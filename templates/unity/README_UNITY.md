# Engine Sound Designer — intégration Unity

1. Copier `Assets/` dans le projet Unity : les sons et `engine_profile.json` sont dans
   `Assets/Resources/EngineSounds/<moteur>/`, le script dans `Assets/EngineSound/EngineSoundPlayer.cs`.
2. Dans les réglages d'import des WAV : *Load Type = Decompress On Load*, *Preload Audio Data* coché
   (les boucles doivent être en mémoire, sans streaming, pour boucler sans trou).
3. Ajouter `EngineSoundPlayer` à un GameObject, `resourceFolder = "EngineSounds/<moteur>"`.
4. Chaque frame : renseigner `rpm`, `throttle`, `load`, `gear`, `speedKmh`, `torque`
   (hybride : `engineOn`, `motorTorque`). Événements : `Shift(newGear)`, `StartEngine()`, `StopEngine()`.

Même algorithme que l'application et le lecteur Godot (rangées charge/état, fondus à puissance
constante entre régimes, `pitch = rpm / rpm_boucle`).

**Non testé ici** : aucune installation Unity sur la machine de développement. Le script n'utilise
que l'API `AudioSource` standard.
