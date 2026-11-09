"""Check native 60 Hz observer ablation and exercise a drivetrain stall/restart."""
import json
from pathlib import Path
import sys
on,off = (json.loads(Path(arg).read_text()) for arg in sys.argv[1:3])
assert len(on) == len(off) == 900
assert [r[0] for r in on] == [r[0] for r in off]
assert any(row[0][9]>0.1 and row[0][2]==1 and row[1][0]>.5 for row in on)
assert any(row[0][1]>2000 and row[1][0]<.01 and row[1][2]<-.08 for row in on)
assert all(row[0][8]==4 for row in on[110:])
assert any(row[0][3] for row in on)
assert not on[-1][0][3] and 800<on[-1][0][1]<950
print('900 identical physics ticks. Shift torque interruption, engine braking, stall, restart and four-wheel contacts passed.')
