# Example: a tiny vehicle model driving EngineSoundPlayer.
# Attach to a Node, set profile_path, run the scene.
# Keys: Up = throttle, Down = brake, E = gear up, Q = gear down.
extends Node

@export_file("*.json") var profile_path: String = ""

var player: EngineSoundPlayer
var gear: int = 1
var speed_ms: float = 0.0
var rpm: float = 900.0
var ratios: Array = [3.6, 2.1, 1.4, 1.0, 0.8]
var final_drive: float = 3.9
var wheel_radius: float = 0.32
var idle_rpm: float = 900.0
var limiter_rpm: float = 6800.0


func _ready() -> void:
	player = EngineSoundPlayer.new()
	player.profile_path = profile_path
	add_child(player)
	if player.profile.has("engine"):
		idle_rpm = float(player.profile.engine.idle_rpm)
		limiter_rpm = float(player.profile.engine.limiter_rpm)
	if player.profile.has("motor"):
		idle_rpm = 0.0
		limiter_rpm = float(player.profile.motor.max_rpm)
	if player.profile.has("transmission"):
		var r: Array = player.profile.transmission.ratios
		if not r.is_empty():
			ratios = r
		final_drive = float(player.profile.transmission.final_drive)
	if player.profile.has("vehicle"):
		wheel_radius = float(player.profile.vehicle.wheel_radius_m)
	player.start_engine()


func _process(delta: float) -> void:
	var throttle := 1.0 if Input.is_key_pressed(KEY_UP) else 0.0
	var brake := 1.0 if Input.is_key_pressed(KEY_DOWN) else 0.0
	if Input.is_key_pressed(KEY_E) and not _e_held:
		_shift(1)
	if Input.is_key_pressed(KEY_Q) and not _q_held:
		_shift(-1)
	_e_held = Input.is_key_pressed(KEY_E)
	_q_held = Input.is_key_pressed(KEY_Q)

	# very simple longitudinal model (the real simulation lives in Engine Sound Designer)
	var ratio: float = float(ratios[gear - 1]) * final_drive
	var accel := throttle * 6.0 / ratio * 3.0 - brake * 8.0 - 0.02 * speed_ms - 0.3
	speed_ms = max(speed_ms + accel * delta, 0.0)
	var wheel_rpm := speed_ms / wheel_radius * 60.0 / TAU
	rpm = clampf(max(wheel_rpm * ratio, idle_rpm), idle_rpm, limiter_rpm)
	if rpm >= limiter_rpm * 0.99 and gear < ratios.size():
		_shift(1)
	player.set_inputs(rpm, throttle, throttle, gear, speed_ms * 3.6, throttle)


var _e_held := false
var _q_held := false


func _shift(dir: int) -> void:
	var g := clampi(gear + dir, 1, ratios.size())
	if g != gear:
		gear = g
		player.shift(gear)
