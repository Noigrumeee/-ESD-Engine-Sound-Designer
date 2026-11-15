// Engine Sound Designer - runtime player for Unity (2021.3+).
// Same algorithm as the application and the Godot player: rows (state + load), equal-power
// crossfade between rpm points, pitch = rpm / loop rpm, smoothed weights.
// Put the exported folder in Assets/Resources/EngineSounds/<engine>/ and set resourceFolder
// to "EngineSounds/<engine>". Untested here (no Unity installation on the build machine).
using System.Collections.Generic;
using UnityEngine;

public class EngineSoundPlayer : MonoBehaviour
{
    public string resourceFolder = "EngineSounds/MyEngine";
    public float volume = 1f;
    public float smoothing = 0.05f;
    public bool spatial = false;

    [Header("Inputs")] public float rpm = 800f, throttle, load, speedKmh, torque;
    public int gear = 1;
    public bool engineOn = true;
    public float motorTorque;

    class Point { public float rpm; public AudioSource src; public float w, t; }
    class Row { public string state; public float load; public List<Point> points = new List<Point>(); }
    readonly Dictionary<string, List<Row>> sets = new Dictionary<string, List<Row>>();
    readonly Dictionary<string, float> setGain = new Dictionary<string, float>();
    readonly Dictionary<string, AudioClip> oneShots = new Dictionary<string, AudioClip>();
    AudioSource oneShot;
    float idleRpm = 800f, duck, duckDb = -6f, master = 1f;
    bool running = true;

    [System.Serializable] class PointJ { public float rpm; public string file; public int loop_samples; }
    [System.Serializable] class RowJ { public string state; public float load; public PointJ[] points; }

    void Start()
    {
        var text = Resources.Load<TextAsset> (resourceFolder + "/engine_profile");
        if (text == null) { Debug.LogError ("EngineSoundPlayer: profile not found in Resources/" + resourceFolder); return; }
        // JsonUtility cannot read dictionaries: the profile is parsed with a tiny dedicated reader.
        var profile = MiniJson.Parse (text.text) as Dictionary<string, object>;
        if (profile.TryGetValue ("engine", out var eng)) idleRpm = System.Convert.ToSingle (((Dictionary<string, object>) eng)["idle_rpm"]);
        var setsJ = (Dictionary<string, object>) profile["sets"];
        foreach (var kv in setsJ)
        {
            var rows = new List<Row>();
            foreach (Dictionary<string, object> rj in (List<object>) ((Dictionary<string, object>) kv.Value)["rows"])
            {
                var row = new Row { state = (string) rj["state"], load = System.Convert.ToSingle (rj["load"]) };
                foreach (Dictionary<string, object> pj in (List<object>) rj["points"])
                {
                    var clip = Resources.Load<AudioClip> (resourceFolder + "/" + StripExt ((string) pj["file"]));
                    if (clip == null) continue;
                    var src = gameObject.AddComponent<AudioSource>();
                    src.clip = clip; src.loop = true; src.volume = 0f; src.spatialBlend = spatial ? 1f : 0f; src.playOnAwake = false;
                    src.Play();
                    row.points.Add (new Point { rpm = System.Convert.ToSingle (pj["rpm"]), src = src });
                }
                row.points.Sort ((a, b) => a.rpm.CompareTo (b.rpm));
                if (row.points.Count > 0) rows.Add (row);
            }
            sets[kv.Key] = rows;
            setGain[kv.Key] = 1f;
        }
        oneShot = gameObject.AddComponent<AudioSource>();
        if (profile.TryGetValue ("one_shots", out var os))
            foreach (var kv in (Dictionary<string, object>) os)
            {
                var c = Resources.Load<AudioClip> (resourceFolder + "/" + StripExt ((string) kv.Value));
                if (c != null) oneShots[kv.Key] = c;
            }
    }

    static string StripExt (string f) { int i = f.LastIndexOf ('.'); return i > 0 ? f.Substring (0, i) : f; }

    public void Shift (int newGear) { gear = newGear; duck = 0.15f; PlayOneShot ("shift"); }
    public void StartEngine() { running = true; PlayOneShot ("start"); }
    public void StopEngine() { running = false; PlayOneShot ("stop"); }
    void PlayOneShot (string n) { if (oneShots.TryGetValue (n, out var c)) oneShot.PlayOneShot (c); }

    static void AddRow (Row row, float r, float w)
    {
        var p = row.points;
        if (r <= p[0].rpm) { p[0].t += w; return; }
        if (r >= p[p.Count - 1].rpm) { p[p.Count - 1].t += w; return; }
        for (int i = 0; i + 1 < p.Count; ++i)
            if (r >= p[i].rpm && r <= p[i + 1].rpm)
            {
                float t = (r - p[i].rpm) / Mathf.Max (p[i + 1].rpm - p[i].rpm, 1e-4f);
                p[i].t += w * Mathf.Cos (t * Mathf.PI * 0.5f);
                p[i + 1].t += w * Mathf.Sin (t * Mathf.PI * 0.5f);
                return;
            }
    }

    static void RowWeights (List<Row> rows, float r, float l)
    {
        if (rows.Count == 0) return;
        rows.Sort ((a, b) => a.load.CompareTo (b.load));
        Row r0 = rows[0], r1 = rows[0]; float u = 0f;
        if (l >= rows[rows.Count - 1].load) r0 = r1 = rows[rows.Count - 1];
        else if (l > rows[0].load)
            for (int i = 0; i + 1 < rows.Count; ++i)
                if (l >= rows[i].load && l <= rows[i + 1].load) { r0 = rows[i]; r1 = rows[i + 1]; u = (l - r0.load) / Mathf.Max (r1.load - r0.load, 1e-4f); break; }
        AddRow (r0, r, r0 == r1 ? 1f : 1f - u);
        if (r0 != r1) AddRow (r1, r, u);
    }

    void Update()
    {
        float dt = Time.deltaTime, k = Mathf.Clamp01 (dt / Mathf.Max (smoothing, 1e-3f));
        master = Mathf.MoveTowards (master, running ? 1f : 0f, dt / (running ? 0.8f : 0.4f));
        float duckGain = 1f;
        if (duck > 0f) { duck -= dt; duckGain = Mathf.Pow (10f, duckDb / 20f); }
        bool hybrid = sets.ContainsKey ("engine") && sets.ContainsKey ("motor");
        foreach (var kv in sets)
        {
            float target = 1f, l = load;
            if (hybrid)
            {
                if (kv.Key == "engine") target = engineOn ? 1f : 0f;
                else { target = (speedKmh > 0.5f || Mathf.Abs (motorTorque) > 0.01f) ? Mathf.Clamp01 (Mathf.Abs (motorTorque) * 2f + 0.2f) : 0f; l = motorTorque; }
            }
            setGain[kv.Key] = Mathf.Lerp (setGain[kv.Key], target, k);
            var onLoad = new List<Row>(); var other = new List<Row>();
            foreach (var row in kv.Value) { foreach (var p in row.points) p.t = 0f; (row.state == "on_load" ? onLoad : other).Add (row); }
            if (kv.Key == "motor" && l < -0.01f && other.Count > 0) RowWeights (other, rpm, l);
            else
            {
                RowWeights (onLoad, rpm, Mathf.Abs (l));
                if (other.Count > 0 && kv.Key == "engine" && throttle < 0.05f && rpm > idleRpm * 1.3f)
                {
                    float o = Mathf.Clamp01 ((0.05f - throttle) / 0.05f);
                    foreach (var row in onLoad) foreach (var p in row.points) p.t *= 1f - o;
                    foreach (var row in other) AddRow (row, rpm, o);
                }
            }
            float gain = setGain[kv.Key] * master * duckGain * volume;
            foreach (var row in kv.Value)
                foreach (var p in row.points)
                {
                    p.w = Mathf.Lerp (p.w, p.t, k);
                    p.src.volume = p.w * gain;
                    p.src.pitch = Mathf.Clamp (Mathf.Max (rpm, 1f) / p.rpm, 0.5f, 2f);
                }
        }
    }
}

/// Minimal JSON reader (objects, arrays, strings, numbers, booleans, null).
public static class MiniJson
{
    public static object Parse (string s) { int i = 0; return Value (s, ref i); }
    static void Ws (string s, ref int i) { while (i < s.Length && char.IsWhiteSpace (s[i])) ++i; }
    static object Value (string s, ref int i)
    {
        Ws (s, ref i);
        char c = s[i];
        if (c == '{')
        {
            var d = new Dictionary<string, object>(); ++i; Ws (s, ref i);
            if (s[i] == '}') { ++i; return d; }
            while (true) { Ws (s, ref i); var key = (string) Value (s, ref i); Ws (s, ref i); ++i; d[key] = Value (s, ref i); Ws (s, ref i); if (s[i++] == '}') return d; }
        }
        if (c == '[')
        {
            var l = new List<object>(); ++i; Ws (s, ref i);
            if (s[i] == ']') { ++i; return l; }
            while (true) { l.Add (Value (s, ref i)); Ws (s, ref i); if (s[i++] == ']') return l; }
        }
        if (c == '"')
        {
            var sb = new System.Text.StringBuilder(); ++i;
            while (s[i] != '"') { if (s[i] == '\\') { ++i; char e = s[i]; sb.Append (e == 'n' ? '\n' : e == 't' ? '\t' : e == 'u' ? (char) System.Convert.ToInt32 (s.Substring (i + 1, 4), 16) : e); if (e == 'u') i += 4; } else sb.Append (s[i]); ++i; }
            ++i; return sb.ToString();
        }
        if (s.Substring (i).StartsWith ("true")) { i += 4; return true; }
        if (s.Substring (i).StartsWith ("false")) { i += 5; return false; }
        if (s.Substring (i).StartsWith ("null")) { i += 4; return null; }
        int st = i;
        while (i < s.Length && "+-0123456789.eE".IndexOf (s[i]) >= 0) ++i;
        return double.Parse (s.Substring (st, i - st), System.Globalization.CultureInfo.InvariantCulture);
    }
}
