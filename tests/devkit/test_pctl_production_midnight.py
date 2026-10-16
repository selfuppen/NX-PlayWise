from __future__ import annotations

from copy import deepcopy
from datetime import datetime, timezone, timedelta
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pctl_production_midnight as midnight
import pctl_standard_evidence as evidence
import pctl_usage_qualification as usage

CONFIG = {"commit":"a"*40,"release_id":"playwise-fixture","run_id":"b"*32,"duration_s":600}
MIDNIGHT = int(datetime(2026,10,17,tzinfo=timezone(timedelta(hours=8))).timestamp())
DAY = 2480


def rows(boundary: bool = False, transient: bool = False) -> list[dict]:
    start={"schema_version":1,"kind":"usage_evidence_start",**{key:CONFIG[key] for key in ('commit','release_id','run_id')},
           "boot_id":"1"*16,"started_ns":"1","duration_ns":"600000000000","interval_ns":"5000000000","append_diagnostics_version":1}
    values=[]
    offsets=[float(second) for second in range(-130,181,5)]
    if boundary:offsets.append(-.1);offsets.sort()
    for number,offset in enumerate(offsets,1):
        before = MIDNIGHT+int(offset//1)
        crosses = offset==-.1
        old=DAY if offset<0 else DAY+1
        after=before+1 if crosses else before
        words=[0]*34;words[:2]=[0x101,1]
        for day in range(7):words[7+day*4+2]=0xffff
        if offset<0 or (transient and offset==0):words[33]=0
        value={"schema_version":1,"kind":"usage_evidence_sample","source":"pctl_1952","scope":"console_usable_time",
               "run_id":CONFIG['run_id'],"boot_id":start['boot_id'],"sample_index":number,
               "begin_ns":str(int((offset+230)*1e9)),"end_ns":str(int((offset+230)*1e9)+100_000_000),
               "unix_before":before,"unix_after":after,"day_before":old,"day_after":DAY+1 if crosses else old,
               "clock_before_valid":True,"clock_after_valid":True,"valid":not crosses,
               "start_timer_calls":"0","timer_enabled":True,"restricted":False,"temporarily_unlocked":False,
               "total_ns":str(int((10000+offset+130 if offset<0 else offset)*1e9)),"signed_remaining_ns":"0",
               "settings_hex":struct.pack('<34H',*words).hex(),**{key:0 for key in evidence.RESULT_FIELDS}}
        values.append(value)
    stop={"schema_version":1,"kind":"usage_evidence_stop","reason":"config_removed_or_changed","samples":len(values),
          "consecutive_failures":0,"save_failures":0,"last_failed_sample":0,"sample_open_retries":0,
          "last_save_errno":0,"last_save_result":0,"last_save_operation":"none"}
    return [start,*values,stop]


def encode(values: list[dict]) -> bytes:
    return ''.join(json.dumps(value)+'\n' for value in values).encode()


def rejected(values: list[dict], label: str) -> None:
    try:midnight.verify(encode(values),CONFIG,DAY)
    except usage.UsageQualificationError:return
    raise AssertionError(label)


def test_boundary() -> None:
    for boundary in (False,True):
        for transient in (False,True):
            result=midnight.verify(encode(rows(boundary,transient)),CONFIG,DAY)
            assert result['qualified'] is False and result['after_day']==2481
            assert bool(result['boundary_invalid_lines_retained']) is boundary
            assert bool(result['initial_unconfigured_lines_retained']) is transient
    for mode in ('no_reset','regression','clock_jump','activation','unexpected_bytes','temporary_bytes','gap','native_limit','pin','failed_result','wrong_day','short_window','mixed_boot','save_failure'):
        values=deepcopy(rows());post=next(i for i,row in enumerate(values) if row.get('day_before')==DAY+1)
        if mode=='no_reset':
            for row in values[post:-1]:row['total_ns']=str(int(row['total_ns'])+10_000_000_000_000)
        elif mode=='regression':values[post+5]['total_ns']='0'
        elif mode=='clock_jump':
            for row in values[post:-1]:row['unix_before']+=30;row['unix_after']+=30
        elif mode=='activation':
            for row in values[post:-1]:row['start_timer_calls']='1'
        elif mode=='unexpected_bytes':
            for row in values[post:-1]:row['settings_hex']='02'+row['settings_hex'][2:]
        elif mode=='temporary_bytes':
            row=values[post+5];row['settings_hex']='02'+row['settings_hex'][2:]
        elif mode=='gap':
            for row in values[post+5:-1]:
                row['begin_ns']=str(int(row['begin_ns'])+20_000_000_000)
                row['end_ns']=str(int(row['end_ns'])+20_000_000_000)
                row['unix_before']+=20;row['unix_after']+=20
                row['total_ns']=str(int(row['total_ns'])+20_000_000_000)
        elif mode=='native_limit':
            row=values[post+5];words=list(struct.unpack('<34H',bytes.fromhex(row['settings_hex'])));words[31:34]=[0x600,0x100,1440];row['settings_hex']=struct.pack('<34H',*words).hex()
        elif mode=='pin':values[post+5]['temporarily_unlocked']=True
        elif mode=='failed_result':values[post+5].update(spent_result=1,valid=False)
        elif mode=='wrong_day':values[post+5].update(day_before=DAY+2,day_after=DAY+2)
        elif mode=='short_window':
            values=[*values[:post+5],values[-1]];values[-1]['samples']=len(values)-2
        elif mode=='mixed_boot':values[post+5]['boot_id']='2'*16
        elif mode=='save_failure':values[-1].update(save_failures=1,last_failed_sample=1,last_save_errno=5,last_save_operation='open')
        rejected(values,mode)
    values=rows(True);cross=next(row for row in values if row.get('valid') is False)
    cross['spent_result']=1;rejected(values,'boundary cannot hide a service error')
    values=rows();values[-1]['samples']-=1;rejected(values,'stop count cannot change')
    values=rows(True);cross=next(row for row in values if row.get('valid') is False)
    words=list(struct.unpack('<34H',bytes.fromhex(cross['settings_hex'])))
    words[31:34]=[0x600,0x100,1440];cross['settings_hex']=struct.pack('<34H',*words).hex()
    rejected(values,'cross-date invalidity cannot hide a 1440 target')
    values=rows();values[0].pop('append_diagnostics_version')
    for row in values[1:-1]:row.pop('sample_index')
    rejected(values,'new production gate requires original sequence diagnostics')


def test_cli() -> None:
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'raw.jsonl').write_bytes(encode(rows(True,True)))
        (root/'config.json').write_text(json.dumps(CONFIG),encoding='utf-8')
        command=[sys.executable,str(ROOT/'tools/pctl_production_midnight.py'),
                 '--raw',str(root/'raw.jsonl'),'--config',str(root/'config.json'),
                 '--before-day',str(DAY),'--output',str(root/'result.json')]
        completed=subprocess.run(command,capture_output=True,text=True,encoding='utf-8')
        assert completed.returncode==0,completed.stderr
        original=(root/'result.json').read_bytes()
        assert json.loads(original)['qualified'] is False
        repeated=subprocess.run(command,capture_output=True,text=True,encoding='utf-8')
        assert repeated.returncode!=0 and (root/'result.json').read_bytes()==original


if __name__=='__main__':
    test_boundary()
    test_cli()
    print('PASS midnight raw: UTC+08 days, retained crossing row, reset/growth, no proxy/1451, exact other-byte preservation and failures')
