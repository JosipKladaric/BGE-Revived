"""BGE Revived demo script.

Run it with the sandbox:

    build\\bin\\bge_sandbox.exe --script assets/scripts/demo.py

The script shows the shape of the scripting API: objects are looked up by name,
transforms are read and written through plain Python types, and per-frame work
is registered on `bge.app.handlers`.
"""

import math

import bge

print("bge %s ready" % bge.__version__)

knot = bge.objects.get("Knot")
marker = bge.objects.get("Marker")
sun = bge.objects.get("Sun")

if knot is None:
    print("no knot in this scene, nothing to animate")
else:
    print("driving '%s' at %s" % (knot.name, knot.worldPosition))

# The classic pattern: a per-frame handler that advances the scene.
def animate():
    dt = bge._native.delta()
    spin = dt * 0.8

    knot.applyRotation((0.0, 0.0, 1.0), spin)
    knot.applyRotation((0.0, 1.0, 0.0), spin * 0.35)

    if marker is not None:
        marker.location = (
            marker.location.x,
            marker.location.y,
            3.0 + math.sin(bge._native.frame() * 0.05) * 0.6,
        )

    if sun is not None:
        # Tilt the key light a little over time.
        light = sun.rotation_euler
        sun.rotation_euler = (light.x + dt * 0.05, light.y, light.z)


bge.app.handlers.post_update.append(animate)

print("objects in scene: %d" % len(bge.objects))
