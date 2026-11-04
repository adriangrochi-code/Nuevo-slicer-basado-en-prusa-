import re, subprocess, sys, os
binary, model, view = sys.argv[1], sys.argv[2], sys.argv[3]
layer = sys.argv[4] if len(sys.argv) > 4 else None
SP = os.path.dirname(os.path.abspath(__file__))
cmd = ['/usr/bin/time', '-v', 'xvfb-run', '-a', '-s', '-screen 0 1440x900x24', binary, f'{model}.stl', '--view', view,
       '--graphics', 'opengl', '--size', '1200x760', '--bench-frames', '20', '--draw-stats', '--screenshot', f'{SP}/m.png']
if layer: cmd += ['--layer', layer]
env = dict(os.environ, HOME=f'{SP}/../ui/home')
p = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=SP, timeout=1200)
o = p.stdout + p.stderr
frames = [(int(c), int(v)) for c, v in re.findall(r'frame \d+: view (\d+) calls, (\d+) vertices', o)]
heavy = [f for f in frames if f[0] > 5]
ms = re.search(r'view: ([\d.]+) ms per frame \(\d+ frames, (\d+) extrusions', o)
rss = int(re.search(r'Maximum resident set size \(kbytes\): (\d+)', o).group(1)) // 1024
print(f'{os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(binary))))} {model} {view} L{layer}: rc={p.returncode} '
      f'ms/frame={ms.group(1) if ms else "?"} extrusions={ms.group(2) if ms else "?"} '
      f'max_calls={max(c for c,_ in frames) if frames else "?"} max_verts={max(v for _,v in frames) if frames else "?"} '
      f'typical_calls={sorted(c for c,_ in heavy)[len(heavy)//2] if heavy else "?"} '
      f'heavy_frames={sum(1 for c,_ in frames if c > 2*sorted(c for c,_ in heavy)[len(heavy)//2]) if heavy else "?"} peakRSS={rss}MB')
