#!/usr/bin/env python3
import json,pathlib,re,statistics,sys

def ms(v):
    n,u=re.fullmatch(r'([\d.]+)(us|ms|s)',v).groups();return float(n)*{'us':.001,'ms':1,'s':1000}[u]
for label in sys.argv[1:]:
    root=pathlib.Path('benchmarks/results')/label
    print(label,'c req/s mean_ms p50_ms p99_ms cpu% cpu_us/request min_rps max_rps success_rps')
    for c in [100,500,1000,5000]:
        rows=[]
        for p in sorted(root.glob(f'r*-c{c}.wrk')):
            t=p.read_text()
            if 'Requests/sec:' not in t:continue
            j=json.loads(p.with_suffix('.json').read_text())
            rps=float(re.search(r'Requests/sec:\s+([\d.]+)',t)[1])
            total=int(re.search(r'(\d+) requests in',t)[1]); bad=re.search(r'Non-2xx or 3xx responses: (\d+)',t)
            good=rps*(1-(int(bad[1]) if bad else 0)/total)
            rows.append([rps,ms(re.search(r'Latency\s+(\S+)',t)[1]),ms(re.search(r'(?m)^\s+50%\s+(\S+)',t)[1]),ms(re.search(r'(?m)^\s+99%\s+(\S+)',t)[1]),j['server_cpu_percent'],j['server_cpu_percent']*1e4/rps,good])
        if rows: print(c,*[round(statistics.median(x),3) for x in list(zip(*rows))[:-1]],round(min(x[0] for x in rows)),round(max(x[0] for x in rows)),round(statistics.median(x[-1] for x in rows),2))
