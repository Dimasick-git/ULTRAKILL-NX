"""Compare the native adapter with measured Unity Editor force results."""
from pathlib import Path
import argparse,hashlib,json,subprocess

def main():
 p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);p.add_argument('--runner',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
 result=json.loads(args.reference.read_text());samples=result['samples'];assert result['unityVersion']=='2022.3.29f1' and len(samples)==56
 scenarios={'normal','write-after','mass-after','deactivate','mode-cycle','inactive-call','kinematic-call'}
 assert {(s['massBefore'],s['mode'],s['scenario']) for s in samples}=={(mass,mode,scenario) for mass in (2,7) for mode in (0,1,2,5) for scenario in scenarios}
 args.output.mkdir(parents=True,exist_ok=True);table=args.output/'force-reference.txt';table.write_text('\n'.join(' '.join([str(s['massBefore']),str(s['mode']),s['scenario']]+[str(s[stage][axis]) for stage in ('immediate','after','second') for axis in 'xyz']) for s in samples)+'\n')
 report=args.output/'force-oracle-validation.json';report.write_text(json.dumps({'status':'running','hardware_verified':False})+'\n')
 run=subprocess.run([str(args.runner),str(table)],text=True,capture_output=True);(args.output/'force-reference-comparison.log').write_text(run.stdout+run.stderr);print((run.stdout+run.stderr)[-1800:]);run.check_returncode()
 source=Path(__file__).resolve().parents[1]/'tests/ForceOracle.cs'
 report.write_text(json.dumps({'status':'host_passed','engine':result['unityVersion'],'reference_cases':56,'worker_configurations':[0,2],'result_sha256':hashlib.sha256(args.reference.read_bytes()).hexdigest(),'oracle_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'center-of-mass force modes, mass-at-call conversion, velocity writes, disable and kinematic transitions; not torque/off-center forces or full gameplay','hardware_verified':False},indent=2)+'\n')

if __name__=='__main__':main()
