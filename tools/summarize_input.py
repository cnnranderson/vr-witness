"""Summarize a role-selected input capture without guessing physical button names.

Usage: python tools/summarize_input.py out/logs/input-....jsonl [--json]
Works for bounded input tests and passive observations beside a live session.
"""
import argparse
from collections import Counter
import json
from pathlib import Path

def summarize(events):
    samples=[e for e in events if e.get("event")=="input.sample" and e.get("tick")]
    if not samples:
        raise ValueError("No valid input samples; repeat only after confirming the native poll is running")
    first=samples[0]["tick"]
    result={"samples":len(samples),"seconds":round((samples[-1]["tick"]-first)/1000,3),
            "modes":dict(Counter(s["mode"] for s in samples)),
            "unfocused_samples":sum(not s["focused"] for s in samples),
            "paused_samples":sum(bool(s["fade"]) for s in samples),"hands":[],"changes":[]}
    for role in range(2):
        hs=[s["hands"][role] for s in samples]
        valid=[h for h in hs if h["valid"]]
        info={"role":"left" if role==0 else "right","valid_samples":len(valid),
              "devices":sorted({h["device"] for h in valid}),"pressed_bits":[],"touched_bits":[],"axes":[]}
        for field in ("pressed","touched"):
            info[field+"_bits"]=[bit for bit in range(64) if any(h[field]&(1<<bit) for h in valid)]
        for axis in range(5):
            if valid:
                ranges=[[min(h["axes"][axis][k] for h in valid),max(h["axes"][axis][k] for h in valid)] for k in range(2)]
                info["axes"].append({"axis":axis,"types":sorted({h["types"][axis] for h in valid}),"xy_range":ranges,
                                     "motion_observed":any(hi-lo>.05 for lo,hi in ranges)})
        result["hands"].append(info)
    previous=None
    for s in samples:
        # Changes are compact press/release and context events, never per-frame noise.
        key=(s["mode"],bool(s["fade"]),bool(s["focused"]),*((h["device"],h["valid"],h["pressed"]) for h in s["hands"]))
        if key!=previous:
            result["changes"].append({"seconds":round((s["tick"]-first)/1000,3),"mode":s["mode"],"paused":bool(s["fade"]),
                                      "pressed":[h["pressed"] for h in s["hands"]]})
            previous=key
    result["limits"]="Sampled state can miss brief edges. A physical label needs a labeled one-control-at-a-time interval; legacy bit names are not physical button identities."
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log",type=Path);parser.add_argument("--json",action="store_true")
    args=parser.parse_args()
    result=summarize([json.loads(l) for l in args.log.read_text(encoding="utf-8-sig").splitlines() if l.strip()])
    if args.json:print(json.dumps(result,indent=2));return
    print(f"{result['samples']} samples / {result['seconds']} seconds; modes {result['modes']}; unfocused {result['unfocused_samples']}; paused {result['paused_samples']}")
    for hand in result["hands"]:
        print(f"{hand['role']}: devices {hand['devices']}; valid {hand['valid_samples']}; pressed bits {hand['pressed_bits']}; touched bits {hand['touched_bits']}")
        for a in hand["axes"]:
            if a["motion_observed"]:print(f"  axis {a['axis']}, types {a['types']}, x/y ranges {a['xy_range']}")
    print(f"{len(result['changes'])} button/context changes; use --json for their timestamps.")
    print(result["limits"])
if __name__=="__main__":main()
