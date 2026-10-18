from __future__ import annotations

import json
from contextlib import redirect_stdout
import io
from pathlib import Path
import struct
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
import tools.qualification.pctl_usage_qualification as usage
import tools.qualification.promote_qualified_build as promoter
import tools.qualification.verify_device_qualification as verifier
import test_qualification_tools as fixtures
from test_pctl_usage_qualification import sample, set_contract


def prepare(packages: Path, reports: Path, mutate=None) -> dict:
    manifest, _, hashes, components = verifier.package_identity(packages)
    report = usage.template(manifest, hashes[verifier.STANDARD_ZIP], components, "mariko-oled", "22.5.0", "1.11.2")
    report.update(report_status="final", observations={key: True for key in usage.DIRECT_OBSERVATIONS},
                  attestation={"operator": "host-fixture-only", "signed_at": 2000},
                  restoration={"settings_before_hex": "01"*68, "settings_restored_hex": "01"*68,
                               "timer_before": False, "timer_after": False, "unexpected_raw_offsets": []})
    values = {
        "usage_live": [sample("before", 0, 10), sample("after", 60, 70)],
        "grant_recovery": [sample("before", 0, 4191, reasons=("daily_allowance",)),
                           sample("after", 1, 4192, minutes=85), sample("active_end", 61, 4252, minutes=85)],
        "direct_cycle": [sample(phase, seconds, used, mode=mode, minutes=0, business_minutes=0,
                                reasons=("eye_care",) if mode=="blocked" else ())
                         for phase, seconds, used, mode in zip(usage.DIRECT_CASES["direct_cycle"],
                             (0,60,181,242,247),(10,70,191,192,197),
                             ("unlimited","unlimited","blocked","unlimited","unlimited"))],
        "reason_handover": [sample("both_blocked",0,100,mode="blocked",minutes=0,business_minutes=0,reasons=("eye_care","dock")),
                            sample("eye_removed",1,100,mode="blocked",minutes=0,business_minutes=0,reasons=("dock",)),
                            sample("released",2,101,mode="unlimited",minutes=0,business_minutes=0)],
    }
    for value, enabled in zip(values["direct_cycle"], (False,True,True,True,False)):
        value["eye_care_enabled"] = enabled
        # Unlimited usage is independent of this timer flag.
        value["timer_enabled"] = False
    if mutate: mutate(values)
    for case, samples in values.items():
        raw_path = reports/f"{case}.jsonl"
        start = {"schema_version":1,"kind":"usage_evidence_start","commit":manifest["commit"],
                 "release_id":manifest["release_id"],"run_id":"c"*32,"boot_id":"1"*16,
                 "started_ns":"99000000000","duration_ns":"600000000000","interval_ns":"5000000000",
                 "append_diagnostics_version":1}
        rows=[start]
        for number, value in enumerate(samples,1):
            value["boot_id"] = start["boot_id"]
            words=[0]*34
            words[:2]=[0x101,1]
            for day in range(7):words[7+day*4+2]=0xffff
            offset=7+((value["day_index"]+3)%7)*4
            words[offset:offset+3] = [0,0,0xffff] if value["pctl_mode"]=="unlimited" else [0x600,0x100,value["configured_minutes"]]
            raw={"schema_version":1,"kind":"usage_evidence_sample","source":"pctl_1952","scope":"console_usable_time",
                 "run_id":start["run_id"],"boot_id":start["boot_id"],"valid":True,"sample_index":number,
                 "begin_ns":value["monotonic_ns"],"end_ns":str(int(value["monotonic_ns"])+value["pairing_span_ns"]),
                 "unix_before":value["captured_at"],"unix_after":value["captured_at"],
                 "day_before":value["day_index"],"day_after":value["day_index"],"clock_before_valid":True,"clock_after_valid":True,
                 "start_timer_calls":str(value["start_timer_calls"]),"timer_enabled":value["timer_enabled"],
                 "signed_remaining_ns":value["signed_remaining_ns"] or "0","restricted":bool(value["restriction_reasons"]),
                 "total_ns":value["total_ns"],"settings_hex":struct.pack('<34H',*words).hex(),"temporarily_unlocked":False}
            raw.update({key:0 for key in ("session_result","timer_result","remaining_result","restricted_result","spent_result","settings_result","unlocked_result")})
            rows.append(raw)
        rows.append({"schema_version":1,"kind":"usage_evidence_stop","reason":"config_removed_or_changed", "samples":len(samples),
                     "consecutive_failures":0,"save_failures":0,"last_failed_sample":0,"sample_open_retries":0,
                     "last_save_errno":0,"last_save_result":0,"last_save_operation":"none"})
        raw_path.write_text(''.join(json.dumps(row)+'\n' for row in rows),encoding='utf-8')
        binding={"path":raw_path.name,"sha256":usage.sha256_file(raw_path)}
        for number,value in enumerate(samples,2):value["raw_source"]=dict(binding,line=number)
        data={"schema_version":1,"kind":"pctl_usage_samples","subject":report["subject"],"release_components":components,
              "samples":samples,"raw_sources":[binding]}
        if case=="grant_recovery":data.update(grant_minutes=15,effective_minutes=15)
        if case=="direct_cycle":data.update(automatic_recovery=True,rest_deadline_unix=1241)
        path=reports/report['cases'][case]['path']
        path.write_text(json.dumps(data),encoding='utf-8');report['cases'][case]['sha256']=usage.sha256_file(path)
        if case=='direct_cycle':
            report['restoration'].update(settings_before_hex=rows[1]['settings_hex'],settings_restored_hex=rows[-2]['settings_hex'],
                before_raw_source=samples[0]['raw_source'],after_raw_source=samples[-1]['raw_source'])
    (reports/usage.REPORT_NAME).write_text(json.dumps(report),encoding='utf-8')
    return report


def test_direct_gate() -> None:
    successes=('ok','post_expiry_use','missing_direct_check')
    for mode in (*successes,'proxy','activation','blocked_consumption','missing_eye_state','wrong_raw_line','old_report','invented_restore','late_endpoint','missing_deadline'):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);packages=fixtures.prepare_packages(root);reports=fixtures.prepare_reports(root)
            set_contract(packages,usage.DIRECT_CONTRACT)
            def mutate(values):
                if mode=='proxy':values['direct_cycle'][1].update(pctl_mode='limit',configured_minutes=1440)
                if mode=='activation':
                    for value in values['direct_cycle'][3:]:value['start_timer_calls']=1
                if mode=='blocked_consumption':values['direct_cycle'][3]['total_ns']=str(197_000_000_000)
                if mode=='missing_eye_state':values['direct_cycle'][1]['eye_care_enabled']=1
                if mode=='post_expiry_use':
                    values['direct_cycle'][3].update(captured_at=1250,monotonic_ns='350000000000',total_ns='195000000000')
                    values['direct_cycle'][4].update(captured_at=1257,monotonic_ns='357000000000',total_ns='202000000000')
                if mode=='late_endpoint':
                    values['direct_cycle'][3].update(captured_at=1260,monotonic_ns='360000000000')
                    values['direct_cycle'][4].update(captured_at=1267,monotonic_ns='367000000000')
            report=prepare(packages,reports,mutate)
            if mode=='missing_deadline':
                path=reports/report['cases']['direct_cycle']['path'];data=json.loads(path.read_bytes());data.pop('rest_deadline_unix')
                path.write_text(json.dumps(data),encoding='utf-8');report['cases']['direct_cycle']['sha256']=usage.sha256_file(path)
                (reports/usage.REPORT_NAME).write_text(json.dumps(report),encoding='utf-8')
            if mode in ('wrong_raw_line','old_report','invented_restore'):
                if mode=='old_report':report['usage_contract']=usage.CONTRACT
                elif mode=='invented_restore':
                    report['restoration']['settings_before_hex']=report['restoration']['settings_restored_hex']='02'*68
                else:
                    path=reports/report['cases']['direct_cycle']['path'];data=json.loads(path.read_bytes())
                    data['samples'][0]['raw_source']['line']=3;path.write_text(json.dumps(data),encoding='utf-8')
                    report['cases']['direct_cycle']['sha256']=usage.sha256_file(path)
                (reports/usage.REPORT_NAME).write_text(json.dumps(report),encoding='utf-8')
            try:result=verifier.verify(packages,reports,'oled','22.5.0','1.11.2')
            except verifier.QualificationError:
                assert mode not in successes,mode
                continue
            assert mode in successes,mode
            assert 'pctl1952_direct_handover' in result['checks'] and 'pctl1952_proxy_handover' not in result['checks']
            if mode=='ok':
                argv=['verify_device_qualification.py','--packages',str(packages),'--reports',str(reports),
                      '--expected-model','oled','--expected-hos','22.5.0','--expected-atmosphere','1.11.2',
                      '--output',str(root/'cli-verification.json')]
                with mock.patch.object(sys,'argv',argv),redirect_stdout(io.StringIO()) as output:
                    assert verifier.main()==0
                assert '无代理' in output.getvalue()
            if mode=='missing_direct_check':result['checks'].remove('pctl1952_direct_handover')
            verification=root/'verification.json';verification.write_text(json.dumps(result),encoding='utf-8')
            try:promoter.promote(packages,verification,root/'qualified')
            except promoter.PromotionError:assert mode=='missing_direct_check'
            else:assert mode in ('ok','post_expiry_use')


if __name__=='__main__':
    test_direct_gate()
    print('PASS direct qualification: retained production raw, true unlimited cycle, legacy isolation, automatic 1451 and promotion gates')
