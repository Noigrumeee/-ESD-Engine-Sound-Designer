# Headless self-test of the exported package:
#   godot --headless --path <package> -s res://tests/selftest.gd
# Prints PASS / FAIL lines and quits with exit code 0 / 1.
extends SceneTree

var player
var frames := 0
var t := 0.0
var step := 0
var failures := 0
var set_name := "engine"


func _check(cond: bool, what: String) -> void:
	print(("PASS " if cond else "FAIL ") + what)
	if not cond:
		failures += 1


func _initialize() -> void:
	var dir := DirAccess.open("res://engine_sounds")
	var name := ""
	for d in dir.get_directories():
		name = d
	player = load("res://addons/engine_sound/engine_sound_player.gd").new()
	player.profile_path = "res://engine_sounds/%s/engine_profile.json" % name
	root.add_child(player)


func _active_players() -> Array:
	var out := []
	for c in player.get_children():
		if c is AudioStreamPlayer and c.stream is AudioStreamWAV and c.stream.loop_mode == AudioStreamWAV.LOOP_FORWARD and not c.stream_paused and c.volume_db > -60.0:
			out.append(c)
	return out


func _process(delta: float) -> bool:
	frames += 1
	t += delta
	if frames < 2:
		return false
	var due := int(t / 0.5) + 1
	if due <= step:
		return false
	step = due
	if step == 1:
		_check(player.profile.size() > 0, "profile loaded")
		_check(player.get_child_count() > 3, "loop players created (%d)" % player.get_child_count())
		var first: AudioStreamPlayer = player.get_child(0)
		_check(first.stream != null and first.stream is AudioStreamWAV, "WAV stream loaded")
		if first.stream is AudioStreamWAV:
			_check(first.stream.loop_mode == AudioStreamWAV.LOOP_FORWARD and first.stream.loop_end > 1000, "loop points set (%d)" % first.stream.loop_end)
		set_name = "engine" if player.profile.sets.has("engine") else "motor"
		var rows: Array = player.profile.sets[set_name].rows
		var mid: float = float(rows[0].points[1].rpm)
		var top_load: float = 0.0
		for r in rows:
			if r.state == "on_load":
				top_load = max(top_load, float(r.load))
		player.set_inputs(mid, 1.0, top_load)
	if step == 2:
		var act := _active_players()
		_check(act.size() >= 1 and act.size() <= 2, "1-2 loops audible at a grid point (%d)" % act.size())
		var any_pitch_one := false
		for p in act:
			if abs(p.pitch_scale - 1.0) < 0.01:
				any_pitch_one = true
		_check(any_pitch_one, "pitch 1.0 on the grid point")
		var rows: Array = player.profile.sets[set_name].rows
		var a: float = float(rows[0].points[0].rpm)
		var b: float = float(rows[0].points[1].rpm)
		player.set_inputs((a + b) * 0.5, 0.5, 0.55)
	if step == 3:
		var act := _active_players()
		_check(act.size() >= 2, "crossfade between neighbours (%d active)" % act.size())
		if set_name == "engine":
			player.set_inputs(float(player.profile.engine.redline_rpm) * 0.7, 0.0, 0.0)
		else:
			player.set_inputs(float(player.profile.motor.max_rpm) * 0.5, 0.0, -0.8)
	if step == 4:
		var act := _active_players()
		var special := false
		var key := "overrun" if set_name == "engine" else "regen"
		for p in act:
			if String(p.stream.resource_path).find(key) >= 0:
				special = true
		_check(special, "lift-off uses the %s loops" % key)
		player.shift(2)
		player.stop_engine()
	if step == 6:
		_check(_active_players().size() == 0, "stop_engine fades the loops out")
		print("RESULT %s" % ("OK" if failures == 0 else "FAILED"))
		quit(0 if failures == 0 else 1)
	return false
