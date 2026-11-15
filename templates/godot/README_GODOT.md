# Engine Sound Designer — intégration Godot 4.4+

## Contenu
```
addons/engine_sound/engine_sound_player.gd   lecteur runtime (class_name EngineSoundPlayer)
engine_sounds/<moteur>/engine_profile.json   profil : grille, fichiers, courbes, transmission
engine_sounds/<moteur>/engine|motor/*.wav    boucles (cycles moteur entiers, départ sur un cycle)
engine_sounds/<moteur>/oneshots/*.wav        démarrage, arrêt, changement de rapport
example/example_vehicle.gd                   exemple minimal (flèches haut/bas, E/Q)
```

## Installation
1. Copier `addons/` et `engine_sounds/` à la racine du projet Godot (`res://`).
2. Laisser Godot importer les WAV (le lecteur règle lui-même les points de boucle).
3. Créer un nœud avec le script `example/example_vehicle.gd` et régler `profile_path` sur
   `res://engine_sounds/<moteur>/engine_profile.json`, ou utiliser directement `EngineSoundPlayer`.

## API
```gdscript
var p := EngineSoundPlayer.new()
p.profile_path = "res://engine_sounds/<moteur>/engine_profile.json"
add_child(p)
p.set_inputs(rpm, throttle, load, gear, speed_kmh, torque)  # chaque image
p.shift(new_gear)          # événement de changement de rapport (creux de volume + son)
p.start_engine() ; p.stop_engine()
p.set_hybrid(engine_on, motor_torque_fraction)   # hybrides : deux jeux (engine + motor)
```
* `load` : charge moteur 0..1 (thermique) ou couple −1..1 (électrique, négatif = régénération).
* Lever de pied d'un thermique : les boucles de décélération (`overrun`) prennent le relais quand
  l'accélérateur est fermé au-dessus du ralenti.

## Principe
Les boucles sont regroupées en rangées (état + charge). Entre deux rangées : mélange linéaire ;
entre deux régimes : fondu à puissance constante ; chaque boucle est jouée à `pitch = rpm / rpm_boucle`.
Aucun fichier n'est chargé pendant le jeu : tous les lecteurs sont créés au chargement et ceux qui
ne sont pas utilisés sont mis en pause.

Pour un moteur positionné en 3D, mettre le nœud sous un `AudioListener3D`… ou remplacer
`AudioStreamPlayer` par `AudioStreamPlayer3D` dans le script (mêmes propriétés).
