# Engine Sound Designer — Architecture technique

Version 1.0.0 (phases 1 à 9). Windows 11 x64, C++20, JUCE 8.0.4, CMake, SQLite, nlohmann/json, Catch2.

## 1. Principe directeur

Quatre couches strictement séparées, dépendances dans un seul sens :

```
            ┌──────────────────────────── src/app  (JUCE, GUI + périphérique audio) ───────────────────────────┐
            │ Pages (Home, Builder, Exhaust, Simulator, Sound Lab, Samples, Export, pages "prévues")          │
            │ Widgets (éditeurs générés depuis le registre de paramètres, courbes, analyseur FFT, schémas)     │
            │ ProjectDocument (undo/redo, autosave)   AudioHost (callback temps réel)   AppContext (services) │
            └───────────────────────────────────────────────▲──────────────────────────────────────────────────┘
                                                            │ (n'importe quel module JUCE ne descend pas plus bas)
            ┌──────────────────────────── src/core  (C++ pur, AUCUNE dépendance JUCE) ─────────────────────────┐
            │ model/   données : Project, specs, architectures, registre de paramètres, presets, JSON          │
            │ sim/     simulation mécanique simplifiée (EngineSim)                                               │
            │ dsp/     briques DSP temps réel (biquads, guides d'onde, retards fractionnaires)                   │
            │ synth/   synthèse (ThermalVoice) + EngineRuntime (sim + synthèse, échantillon par échantillon)     │
            │ render/  rendu offline déterministe + écriture WAV + analyse                                       │
            │ library/ index SQLite de la bibliothèque                                                           │
            └────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

`esd_core` est une bibliothèque statique sans JUCE : elle est testée seule (Catch2), utilisée par l'outil
`esd_render`, et sera la base de la future bibliothèque runtime native pour les jeux (phase 8).

## 2. Modules et responsabilités

| Module | Fichiers | Responsabilité | Interface |
|---|---|---|---|
| base | `Math.h`, `Random.h`, `Curve.*` | maths, aléatoire déterministe (xorshift64*), courbes éditables (monotone cubique) + table de lecture sans allocation | `Curve::eval`, `CurveTable` |
| model | `EngineLayout.*` | architectures (en ligne, V, boxer, étoile, Wankel, moto), géométrie du vilebrequin, ordres d'allumage | `computeFiringPlan`, `possibleFiringOrders` |
| model | `Specs.h`, `Project.*` | données du moteur, admission, échappement, suralimentation, mixage, samples ; validation / correction | `validate`, `sanitize` |
| model | `Params.*` | **registre unique** des paramètres : plage, unité, aide, niveau de modélisation (M/A/S) | `paramRegistry`, `paramsInGroup` |
| model | `Serialization.*` | JSON tolérant (clés inconnues ignorées, clés absentes = défaut, sauvegarde atomique) | `projectToJson`, `loadProjectFile` |
| model | `Presets.*` | 13 moteurs de démonstration complets | `makePreset` |
| sim | `EngineSim.*` | dynamique du vilebrequin, ralenti, limiteur, coupure en décélération, turbo, températures | `step(controls, dt)` |
| dsp | `Filters.*`, `Tube.*` | biquads RBJ, filtres 1 pôle, guide d'onde 1D (tube) avec retard fractionnaire Hermite | `process(x)` |
| synth | `ThermalVoice.*` | génération sonore par couches à partir de l'angle vilebrequin | `prepare` (alloue) / `process` (temps réel) |
| synth | `EngineRuntime.*` | sim + voix, sim à pas de 32 échantillons, voix interpolée par échantillon | `process`, `telemetry` |
| render | `Render.*` | rendu offline (script de commandes, banc à régime fixe, démo), stems par couche, WAV 16/24/32f | `renderScript`, `renderDyno`, `writeWav` |
| library | `ProjectLibrary.*` | index SQLite (projets, catégories, récents, favoris) | `upsert`, `all`, `recent` |
| app | `AudioHost.*` | périphérique audio (WASAPI par défaut, ASIO optionnel), échange sans verrou du runtime, FIFO d'analyse, pré-écoute de samples | atomics + `setProject` |
| app | `ProjectDocument.*` | projet ouvert, historique annuler/rétablir (instantanés JSON, regroupement des glissements), autosave 30 s | `edit(fn, desc, key)` |
| app | `pages/*`, `widgets/*` | interface | `Page::projectChanged` |

## 3. Threads et temps réel

* **Thread audio** (`AudioHost::audioDeviceIOCallbackWithContext`) : aucune allocation, aucun verrou, aucun accès disque.
  * Le `EngineRuntime` est construit (allocations) sur le thread message, passé via `std::atomic<EngineRuntime*> pending`.
  * Le thread audio l'adopte en début de bloc, recopie l'état mécanique de l'ancien (régime, températures, turbo…)
    et fait un fondu de 1024 échantillons ; l'ancien repart via `retired` et est détruit par un timer du thread message.
  * Commandes (accélérateur, charge, contact, démarreur, banc…) et mixage : `std::atomic`.
  * Télémétrie : `std::atomic` publiés à chaque bloc. Analyse : `juce::AbstractFifo` sans verrou.
  * Coût mesuré : ~35× plus rapide que le temps réel pour un V8 en rendu offline (≈ 3 % CPU en direct).
* **Thread message** : GUI, édition, construction des runtimes, autosave.
* **Threads de rendu** (Export Studio) : copie du projet, rendu sur `std::thread`, annulation par `std::atomic<bool>`,
  progression par atomique : l'interface ne bloque jamais.

Une modification **structurelle** (géométrie, échappement…) reconstruit le runtime (quelques dizaines de µs + fondu).
Une modification de **mixage** ne touche que les atomiques (`ChangeKind::MixOnly`).

## 4. Données et fichiers

* Projet : `*.esdproj` (JSON, `schema_version`), sauvegarde atomique (fichier temporaire puis renommage).
* Bibliothèque : `Documents\EngineSoundDesigner\Projects\` (+ `Demo presets\` créé au premier lancement).
* Index SQLite : `%APPDATA%\EngineSoundDesigner\library.db` (reconstructible : les fichiers font foi).
* Autosave : `%APPDATA%\EngineSoundDesigner\autosave\<id>.esdproj` (récupérable depuis Home).
* Réglages (périphérique audio) : `%APPDATA%\EngineSoundDesigner\EngineSoundDesigner.settings`.
* Samples : référencés (chemin), jamais copiés ; région, gain, boucle et mapping régime/charge non destructifs.

## 5. Points d'extension

* **Nouvelle architecture moteur** : ajouter un `LayoutDef` dans `buildLibrary()` (ordre de référence + intervalles +
  bancs) ; la géométrie des manetons est dérivée et les ordres alternatifs sont énumérés automatiquement.
* **Nouveau type de motorisation** (électrique phase 4, hybride phase 6) : `PowertrainKind` est déjà dans le projet ;
  ajouter une simulation (`sim/`) et une voix (`synth/`) avec la même interface `prepare/process` ; `EngineRuntime`
  deviendra un assemblage de sources (thermique, électrique, transmission).
* **Nouvelle méthode de synthèse / nouvelle couche** : `LayerId` + un bloc dans la voix ; le mixeur, les stems et les
  mesures suivent automatiquement (`kNumLayers`).
* **Nouveau paramètre** : une entrée dans `Params.cpp` suffit pour obtenir éditeur, infobulle, badge M/A/S et bornage.
* **Nouveau format d'export** : `render/` (WAV écrit à la main ; FLAC via JUCE côté app), profils runtime en phase 8.

## 6. Modules ajoutés par les phases 3 à 8

| Module | Fichiers | Responsabilité |
|---|---|---|
| synth | `LayerBus.*` | bus de couches (15 couches) ; `LayerMixer` : chaîne d'effets par couche, automatisations, réverbération partagée, étage maître |
| dsp | `Effects.*` | saturation, compresseur, réverbération type Freeverb, FFT radix-2, convolution FFT partitionnée (bloc 256) |
| sim | `ElectricSim.*` | machine électrique : enveloppe couple/puissance, boucle de courant, courant, échauffement, régénération |
| synth | `ElectricVoice.*` | 9 couches électriques (électromagnétique, sifflement, rotor, vibrations, roulements, onduleur, refroidissement, résonances ; transmission via `TransmissionVoice`) |
| sim | `Drivetrain.*` | boîte (manuelle, convertisseur, DCT, CVT, AMT, EV) + véhicule ; système distinct qui fournit au moteur charge et inertie |
| synth | `TransmissionVoice.*` | sifflement d'engrenages, claquements de passage, glissement du convertisseur, chaîne de CVT |
| synth | `SampleLayer.*` | samples mappés (régime, charge) : interpolation 2D, pitch = régime / régime du sample (même algorithme que les lecteurs de jeu) |
| synth | `EngineRuntime.*` | runtime unifié thermique / électrique / hybride (+ logique hybride, SOC), mode banc / libre / véhicule |
| render | `LoopGenerator.*` | grille de boucles de cycles entiers, raccords, contrôle de continuité, normalisation, multithread, reprise, profil runtime JSON |
| render | `GameExport.*`, `templates/` | paquets Godot (testé headless), Unity (C#), Unreal (C++ + CSV DataTable) |
| render | `AudioFile.*` | lecteur WAV du cœur ; côté application, JUCE décode WAV/FLAC/AIFF et écrit le FLAC |

Le moteur, la transmission et le véhicule restent des systèmes séparés : à chaque pas de simulation la transmission
calcule la charge et l'inertie vues par la source (`Coupling`), la source intègre sa vitesse, puis la transmission fait
avancer le véhicule (`advance`). Les fermetures d'embrayage / lock-up recalent la vitesse de la source (`snapOmega`).

## 7. Avancement des phases

| Phase | Contenu | État |
|---|---|---|
| 1 Fondations | CMake, JUCE, interface, projets, audio temps réel, samples, synthèse, pré-écoute | fait |
| 2 Premier moteur thermique | 13 architectures de démonstration | fait |
| 3 Sound Lab | effets par couche, automatisations multi-sources, réverbération, convolution, couche de samples interpolée | fait |
| 4 Électrique | PMSM, BLDC, induction (+ flux axial, rotor externe) | fait |
| 5 Transmission & véhicule | manuelle, convertisseur, DCT, CVT, AMT, EV fixe / multi-vitesses, véhicule | fait |
| 6 Hybride | mild, full, plug-in, série, parallèle, série-parallèle | fait |
| 7 Loop Generator | grille, cycles entiers, raccords, multithread, reprise | fait |
| 8 Export & intégration | WAV, FLAC, JSON, Godot (testé), Unity, Unreal | fait (Unity / Unreal non testés : pas d'installation) |
| 9 Finition | tests (47), presets (18), documentation, calibration des niveaux | fait |

Limites connues : pas de lecteur temps réel dans les jeux autre que les scripts fournis (pas de bibliothèque native
compilée par moteur de jeu) ; l'éditeur multipiste du Sound Lab est un mixeur par couches avec effets et automatisations,
pas une timeline ; les scripts Unity et Unreal n'ont pas pu être compilés ici.
