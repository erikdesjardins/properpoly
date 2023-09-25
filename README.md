# properpoly

RK-002 PolyMUX firmware with better polyphony.

Muxes to the lowest available channel (non round-robin), or evicts the oldest keypress.

---

vs. `RKPOLYMUXALLOCMODE_ROUNDROBIN`:

Steals notes in a less intrusive way (the oldest press, rather than round-robin).

vs. `RKPOLYMUXALLOCMODE_FIRSTFREE`:

Can steal notes, which avoids glitches/missed notes when you don't quite release a key fast enough.

vs. both:

Only uses the N lowest channels necessary, so you can record a monophonic track, or in limited polyphony, without reconfiguring anything. Just press fewer keys at once.

## Flashing

See https://duy.retrokits.com.
