"""Check pointer turnover and invalid cursor data without touching a game."""
import json
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from puzzle_aim_trace import sample
from test_controller_trace import Memory

class CursorMemory(Memory):
    def __init__(self, cursor=0x60000, replace=False, **kwargs):
        super().__init__(**kwargs)
        self.cursor=cursor;self.replace=replace;self.cursor_queries=0;self.cursor_reads=0
    def read(self,address,size):
        if address==0x62D4D0:
            self.cursor_queries+=1
            return struct.pack("<Q",self.cursor+(0x1000 if self.replace and self.cursor_queries>1 else 0))
        if address==0x60000:
            self.cursor_reads+=1
            raw=bytearray(size)
            struct.pack_into("<2f",raw,0x18,.3,float('nan'))
            struct.pack_into("<3f",raw,0x9C,1,2,3)
            return bytes(raw)
        return super().read(address,size)

class Guards(unittest.TestCase):
    def test_cursor_turnover_rejected(self):
        with self.assertRaisesRegex(ValueError,'metadata changed'):sample(CursorMemory(replace=True),0)
    def test_null_cursor_not_dereferenced(self):
        m=CursorMemory(cursor=0)
        self.assertNotIn('cursor',sample(m,0));self.assertEqual(m.cursor_reads,0)
    def test_invalid_floats_and_pose_retained(self):
        result=sample(CursorMemory(),0)
        self.assertAlmostEqual(result['cursor']['screen_coordinates'][0],.3)
        self.assertIsNone(result['cursor']['screen_coordinates'][1])
        self.assertEqual(result['cursor']['ray_origin'],[1,2,3])
        self.assertNotIn('hand',result['controllers'][0])
        json.dumps(result,allow_nan=False)
    def test_controller_guards_still_apply(self):
        m=CursorMemory(count=3)
        with self.assertRaisesRegex(ValueError,'Unexpected controller array'):sample(m,0)
        self.assertEqual(m.array_reads,0)

if __name__=='__main__':unittest.main()
