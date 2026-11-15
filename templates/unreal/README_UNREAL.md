# Engine Sound Designer — intégration Unreal Engine 5

1. Copier `Content/EngineSounds/<moteur>/` dans le dossier `Content/` du projet et importer les WAV
   (glisser-déposer dans le Content Browser en conservant l'arborescence `engine/`, `motor/`, `oneshots/`).
2. Copier `Source/EngineSound/EngineSoundComponent.h/.cpp` dans le module C++ du jeu
   (ajouter `"Json"` aux `PublicDependencyModuleNames` du `.Build.cs`).
3. Ajouter `EngineSoundComponent` au véhicule : `ProfilePath = EngineSounds/<moteur>/engine_profile.json`,
   `SoundFolder = /Game/EngineSounds/<moteur>`.
4. Chaque tick : `SetInputs(Rpm, Throttle, Load, Gear, SpeedKmh)` ; `Shift(NewGear)` aux changements de rapport.

`EngineSoundGrid.csv` est importable comme DataTable (une ligne par boucle : jeu, état, charge, régime,
fichier) pour une implémentation en Blueprint / MetaSound sans C++.

**Non testé ici** : aucune installation d'Unreal sur la machine de développement.
