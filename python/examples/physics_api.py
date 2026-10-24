"""Tour of the physics API on any playing scene (no agents required).

In the editor enable Training > Python Bridge (e.g. with File > Showcases > Physics
loaded), then:

    python python/examples/physics_api.py

The scene advances only when this script calls simulate(), so every step is under
its control.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from phoenix_env import PhoenixSim  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=5555)
    args = parser.parse_args()

    sim = PhoenixSim(port=args.port)
    sim.reset(seed=0)
    physics = sim.physics

    info = physics.info()
    print(f"gravity {info['gravity']}, dt {info['timestep']:.4f}s, {info['bodies']} bodies")

    bodies = physics.bodies()
    dynamic = [b for b in bodies if b["type"] == "dynamic"]
    for b in bodies[:10]:
        print(f"  #{b['id']:<4} {b['tag']:<24} {b['type']:<9} mass {b['mass']:7.2f} kg  at {b['position']}")
    if not dynamic:
        sys.exit("no dynamic bodies in this scene")

    # Launch the first dynamic body upward and follow it for one simulated second.
    target = dynamic[0]["id"]
    physics.apply_impulse(target, [0.0, 6.0 * max(1.0, dynamic[0]["mass"]), 0.0])
    for _ in range(6):
        physics.simulate(10)
        b = physics.body(target)
        print(f"  t={physics.info()['time']:.2f}s  y={b['position'][1]:+.2f}  vy={b['velocity'][1]:+.2f}")

    # What is below it?
    b = physics.body(target)
    hit = physics.raycast(b["position"], [0, -1, 0], max_distance=50, ignore=target)
    if hit:
        print(f"ray down hits '{hit['tag']}' at {hit['distance']:.2f} m, normal {hit['normal']}")

    # Contacts that happened while it fell back.
    physics.simulate(120)
    for c in physics.contacts()[:8]:
        print(f"  contact {'begin' if c['entered'] else 'end  '} {c['tag_a']} <-> {c['tag_b']} at {c['time']:.2f}s")

    # Low gravity, a sideways shove, and a bouncier material.
    physics.set_gravity([0.0, -1.62, 0.0])
    physics.set_material(target, restitution=0.8)
    physics.set_body(target, velocity=[3.0, 4.0, 0.0])
    physics.simulate(60)
    print(f"on the moon: {physics.body(target)['position']}")
    physics.set_gravity(info["gravity"])
    sim.close()


if __name__ == "__main__":
    main()
