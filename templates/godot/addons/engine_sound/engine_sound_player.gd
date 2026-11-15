# Engine Sound Designer - runtime player for Godot 4.4+
# Plays a loop set exported by Engine Sound Designer (engine_profile.json + loops).
#
# Same algorithm as the application's sample layer:
#  * loops are grouped in rows (state + load); the two rows around the current load are
#    blended linearly, the two loops around the current rpm are crossfaded with equal power;
#  * every loop plays at pitch = rpm / loop_rpm, so the pitch follows the engine between points;
#  * the loops hold whole engine cycles and start on a cycle boundary: they stay coherent when
#    crossfaded; weights are smoothed (no clicks), inactive players are paused.
#
# Usage:
#   var p := EngineSoundPlayer.new()
#   p.profile_path = "res://engine_sounds/my_v8/engine_profile.json"
#   add_child(p)
#   ... every frame: p.set_inputs(rpm, throttle, load, gear, speed_kmh, torque)
#   on gear change: p.shift(new_gear) ; p.start_engine() / p.stop_engine()
#   hybrids: p.set_hybrid(engine_on, motor_torque_fraction)
class_name EngineSoundPlayer
extends Node

@export_file("*.json") var profile_path: String = ""
@export var bus: StringName = &"Master"
@export var volume_db: float = 0.0
@export var autostart: bool = true
## Seconds for weight changes (crossfade smoothing).
@export var smoothing: float = 0.05

var rpm: float = 800.0
var throttle: float = 0.0
var engine_load: float = 0.0
var gear: int = 1
var speed_kmh: float = 0.0
var torque: float = 0.0
var engine_on: bool = true
var motor_torque: float = 0.0

var profile: Dictionary = {}
var _base_dir: String = ""
var _sets: Dictionary = {}      # set name -> Array of rows {state, load, points: [{rpm, player, w}]}
var _set_gain: Dictionary = {}  # set name -> smoothed gain
var _one_shots: Dictionary = {}
var _oneshot_player: AudioStreamPlayer
var _running: bool = false
var _master: float = 0.0        # 0..1 fade of the loops (start / stop)
var _duck_t: float = 0.0
var _idle_rpm: float = 800.0


func _ready() -> void:
	if profile_path != "":
		load_profile(profile_path)
	if autostart:
		_running = true
		_master = 1.0


func load_profile(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("EngineSoundPlayer: cannot open %s" % path)
		return false
	var data = JSON.parse_string(f.get_as_text())
	if typeof(data) != TYPE_DICTIONARY or data.get("format", "") != "EngineSoundDesigner.runtime_profile":
		push_error("EngineSoundPlayer: %s is not an Engine Sound Designer runtime profile" % path)
		return false
	for c in get_children():
		c.queue_free()
	profile = data
	_base_dir = path.get_base_dir()
	_sets.clear()
	if profile.has("engine"):
		_idle_rpm = float(profile.engine.get("idle_rpm", 800.0))
	for set_name in profile.sets.keys():
		var rows: Array = []
		for row in profile.sets[set_name].rows:
			var pts: Array = []
			for p in row.points:
				var stream := _load_stream(_base_dir.path_join(p.file))
				if stream == null:
					continue
				if stream is AudioStreamWAV:
					stream.loop_mode = AudioStreamWAV.LOOP_FORWARD
					stream.loop_begin = 0
					stream.loop_end = int(p.loop_samples)
				var player := AudioStreamPlayer.new()
				player.stream = stream
				player.bus = bus
				player.volume_db = -80.0
				add_child(player)
				player.play()
				player.stream_paused = true
				pts.append({"rpm": float(p.rpm), "player": player, "w": 0.0})
			pts.sort_custom(func(a, b): return a.rpm < b.rpm)
			if pts.size() > 0:
				rows.append({"state": str(row.state), "load": float(row.load), "points": pts})
		_sets[set_name] = rows
		_set_gain[set_name] = 0.0
	_oneshot_player = AudioStreamPlayer.new()
	_oneshot_player.bus = bus
	add_child(_oneshot_player)
	_one_shots.clear()
	for k in profile.get("one_shots", {}).keys():
		var s := _load_stream(_base_dir.path_join(profile.one_shots[k]))
		if s != null:
			if s is AudioStreamWAV:
				s.loop_mode = AudioStreamWAV.LOOP_DISABLED
			_one_shots[k] = s
	return true


func _load_stream(path: String) -> AudioStream:
	# imported resources (inside the project) or raw files on disk (Godot 4.4+)
	if ResourceLoader.exists(path):
		return load(path) as AudioStream
	if path.get_extension().to_lower() == "wav":
		return AudioStreamWAV.load_from_file(path)
	push_warning("EngineSoundPlayer: unsupported or missing file %s (Godot plays WAV/OGG, export the set as WAV)" % path)
	return null


func set_inputs(p_rpm: float, p_throttle: float, p_load: float = -INF, p_gear: int = -1, p_speed_kmh: float = -1.0, p_torque: float = 0.0) -> void:
	rpm = maxf(p_rpm, 0.0)
	throttle = clampf(p_throttle, 0.0, 1.0)
	# load: engine load 0..1, or motor torque -1..1 (negative = regeneration); omitted = throttle
	engine_load = throttle if is_inf(p_load) else clampf(p_load, -1.0, 1.0)
	if p_gear >= 0:
		gear = p_gear
	if p_speed_kmh >= 0.0:
		speed_kmh = p_speed_kmh
	torque = p_torque


func set_hybrid(p_engine_on: bool, p_motor_torque_fraction: float) -> void:
	engine_on = p_engine_on
	motor_torque = clampf(p_motor_torque_fraction, -1.0, 1.0)


func shift(new_gear: int) -> void:
	gear = new_gear
	var ev: Dictionary = profile.get("events", {}).get("shift", {})
	_duck_t = float(ev.get("duck_s", 0.15))
	_play_one_shot("shift")


func start_engine() -> void:
	_running = true
	_play_one_shot("start")


func stop_engine() -> void:
	_running = false
	_play_one_shot("stop")


func _play_one_shot(name: String) -> void:
	if _one_shots.has(name) and _oneshot_player != null:
		_oneshot_player.stream = _one_shots[name]
		_oneshot_player.play()


# Interpolation weights inside one family of rows (same as the C++ SampleLayer::computeWeights).
func _row_weights(rows: Array, p_rpm: float, p_load: float) -> void:
	for row in rows:
		for p in row.points:
			p["t"] = 0.0
	if rows.is_empty():
		return
	var sorted := rows.duplicate()
	sorted.sort_custom(func(a, b): return a.load < b.load)
	var r0 = sorted[0]
	var r1 = sorted[0]
	var u := 0.0
	if p_load >= sorted[-1].load:
		r0 = sorted[-1]
		r1 = sorted[-1]
	elif p_load > sorted[0].load:
		for i in range(sorted.size() - 1):
			if p_load >= sorted[i].load and p_load <= sorted[i + 1].load:
				r0 = sorted[i]
				r1 = sorted[i + 1]
				u = (p_load - r0.load) / maxf(r1.load - r0.load, 0.0001)
				break
	_add_row(r0, p_rpm, 1.0 - u if r0 != r1 else 1.0)
	if r0 != r1:
		_add_row(r1, p_rpm, u)


func _add_row(row: Dictionary, p_rpm: float, w: float) -> void:
	var pts: Array = row.points
	if p_rpm <= pts[0].rpm:
		pts[0].t += w
		return
	if p_rpm >= pts[-1].rpm:
		pts[-1].t += w
		return
	for i in range(pts.size() - 1):
		var a: float = pts[i].rpm
		var b: float = pts[i + 1].rpm
		if p_rpm >= a and p_rpm <= b:
			var t := (p_rpm - a) / maxf(b - a, 0.0001)
			pts[i].t += w * cos(t * PI * 0.5)       # equal power crossfade
			pts[i + 1].t += w * sin(t * PI * 0.5)
			return


func _process(delta: float) -> void:
	if _sets.is_empty():
		return
	_master = move_toward(_master, 1.0 if _running else 0.0, delta / (0.8 if _running else 0.4))
	var duck := 1.0
	if _duck_t > 0.0:
		_duck_t -= delta
		duck = db_to_linear(float(profile.get("events", {}).get("shift", {}).get("duck_db", -6.0)))
	var k := clampf(delta / maxf(smoothing, 0.001), 0.0, 1.0)
	var is_hybrid: bool = _sets.has("engine") and _sets.has("motor")
	for set_name in _sets.keys():
		var rows: Array = _sets[set_name]
		var set_target := 1.0
		var r := rpm
		var l := engine_load
		if is_hybrid:
			if set_name == "engine":
				set_target = 1.0 if engine_on else 0.0
			else:
				set_target = clampf(absf(motor_torque) * 2.0 + 0.2, 0.0, 1.0) if speed_kmh > 0.5 or absf(motor_torque) > 0.01 else 0.0
				l = motor_torque
		_set_gain[set_name] = lerpf(float(_set_gain[set_name]), set_target, k)
		# choose the row family: overrun (lift-off) for engines, regeneration for motors
		var on_load: Array = []
		var other: Array = []
		for row in rows:
			if row.state == "on_load":
				on_load.append(row)
			else:
				other.append(row)
		for row in rows:
			for p in row.points:
				p["t"] = 0.0
		if set_name == "motor" and l < -0.01 and not other.is_empty():
			_row_weights(other, r, l)
		else:
			_row_weights(on_load, r, absf(l))
			if not other.is_empty() and set_name == "engine" and throttle < 0.05 and r > _idle_rpm * 1.3:
				var o := clampf((0.05 - throttle) / 0.05, 0.0, 1.0)
				for row in on_load:
					for p in row.points:
						p.t *= (1.0 - o)
				for row in other:
					_add_row(row, r, o) # overrun rows are weighted on rpm only
		var gain: float = float(_set_gain[set_name]) * _master * duck * db_to_linear(volume_db)
		for row in rows:
			for p in row.points:
				p.w = lerpf(float(p.w), float(p.t), k)
				var player: AudioStreamPlayer = p.player
				var w: float = p.w * gain
				if w < 0.0005:
					if not player.stream_paused:
						player.stream_paused = true
					continue
				if player.stream_paused:
					player.stream_paused = false
				player.volume_db = linear_to_db(w)
				player.pitch_scale = clampf(maxf(r, 1.0) / p.rpm, 0.5, 2.0)
