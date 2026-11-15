# Engine Sound Designer 1.0

Application Windows native (C++20 / JUCE) de conception, simulation, écoute et export de sons de moteurs pour le jeu vidéo :
moteurs thermiques, électriques et hybrides, boîte de vitesses et véhicule, Sound Lab multicouche, générateur de boucles
et exports Godot / Unity / Unreal. Voir [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) et [docs/MODEL.md](docs/MODEL.md).

## Lancer
`build\Release\src\app\EngineSoundDesignerApp_artefacts\Release\Engine Sound Designer.exe`

## Compiler
Visual Studio 2022 Build Tools (C++). Les dépendances (JUCE 8.0.4, Catch2 3.7.1, nlohmann/json 3.11.3, SQLite 3.49.1)
ne sont pas dans le dépôt : `setup_deps.bat` les télécharge dans `external/`.
```bat
setup_deps.bat
build.bat            :: Release -> build\Release
```
ASIO (optionnel) : `set ESD_CMAKE_ARGS=-DESD_ENABLE_ASIO=ON -DESD_ASIO_SDK_DIR=C:\chemin\asiosdk` puis `build.bat`.

## Espaces de travail
| Espace | Contenu |
|---|---|
| Home | presets (13 thermiques, 3 électriques, 2 hybrides), bibliothèque SQLite, récents, sessions autosauvées |
| Engine Builder | architecture, vilebrequin, angle de V, ordre d'allumage validé, géométrie, distribution, combustion, admission, suralimentation, courbe de couple |
| Electric Motor | PMSM / BLDC / induction / flux axial / rotor externe : pôles, encoches, onduleur, PWM, refroidissement ; enveloppe couple/puissance |
| Hybrid Powertrain | mild, full, plug-in, série, parallèle, série-parallèle : batterie, seuils EV, recharge, régénération |
| Exhaust Designer | collecteurs, routage simple/double/X/H, catalyseur, résonateur, silencieux, sorties ; schéma |
| Sound Lab | mixeur par couche, chaîne d'effets par couche (EQ, filtres, saturation, compresseur, convolution IR, réverbération), automatisations par courbes |
| Engine Simulator | démarreur, scénarios, mode banc, mode véhicule, télémétrie, FFT / spectrogramme / oscilloscope |
| Sample Editor | import WAV/FLAC/AIFF, découpe, normalisation, recherche de point de boucle, mapping régime/charge (couche Samples interpolée) |
| Transmission & Vehicle | manuelle, auto à convertisseur, DCT, CVT, AMT, réducteur EV (+ rapports simulés), multi-vitesses EV ; véhicule ; conduite en direct |
| Loop Generator | grille régime × charge (+ décélération / régénération), boucles de cycles entiers, raccords contrôlés, multithread, reprise |
| Export Studio | rendus WAV/FLAC, stems, paquets Godot / Unity / Unreal |

Clavier : **Espace** moteur, **flèche haut** plein gaz, **flèche bas** frein, **E / Q** rapport + / −, **0-9** accélérateur,
Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+Z / Ctrl+Y.

## Tester
1. `build\Release\tests\esd_tests.exe` : 47 cas (géométrie, simulation, électrique, transmission, hybride, effets,
   automatisation, boucles, export, JSON, SQLite).
2. Ligne de commande (`build\Release\tools\esd_render.exe`) :
   ```bat
   esd_render --list
   esd_render --preset v8_cross --drive --out v8_drive.wav          :: véhicule : départ, rapports, lever de pied, freinage
   esd_render --preset ev_pmsm --rpm 6000 --load 0.8 --analyze
   esd_render --preset v8_cross --loops C:\temp\v8_loops
   esd_render --preset v8_cross --export godot C:\temp\exports
   esd_render --calibrate
   ```
3. Paquet Godot : ouvrir le dossier exporté avec Godot 4.4+ (il contient `project.godot` et une scène d'exemple), ou
   l'autotest : `godot --headless --path <paquet> -s res://tests/selftest.gd` (vérifié avec Godot 4.5.1).
4. Démos : `esd_render --preset <id> --demo|--drive --out x.wav` (les WAV de démo ne sont pas versionnés).

## Organisation
```
src/core   C++ pur : model / sim / dsp / synth / render / library   (aucune dépendance JUCE)
src/app    application JUCE : pages, widgets, hôte audio, document
templates  code d'intégration Godot / Unity / Unreal (copié à côté des exécutables)
tests      Catch2          tools   esd_render (CLI)          docs   architecture et modèle
```
