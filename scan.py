"""更详细地分析 Mars PAK 结构"""
import zipfile
from collections import defaultdict

PAK = r'c:\Users\nico_\Desktop\solar\textures\textures\planets\Mars-Surface-PBC.pak'
zf = zipfile.ZipFile(PAK)

all_names = zf.namelist()
jpg_files = [n for n in all_names if n.endswith('.jpg')]
print(f"Total JPG: {len(jpg_files)}")

# 按面 + 层级分组
level_data = defaultdict(lambda: defaultdict(list))
face_names = ['pos_x','neg_x','pos_y','neg_y','pos_z','neg_z']

for n in jpg_files:
    if '/' not in n or 'base' in n:
        continue
    parts = n.strip('/').split('/')
    face = None
    for p in parts:
        if p in face_names: face = p; break
    if not face: continue
    # e.g. "0_0_0.jpg" → level=0, x=0, y=0
    fname = parts[-1]
    segs = fname.rsplit('.',1)[0].split('_')
    if len(segs) < 3: continue
    try:
        lvl = int(segs[0])
    except: continue
    level_data[lvl][face].append(n)

print("\n--- Levels found ---")
for lvl in sorted(level_data.keys()):
    counts = {f: len(level_data[lvl][f]) for f in face_names if f in level_data[lvl]}
    grid_size = int(counts[max(counts, key=counts.get)] ** 0.5)
    print(f"  LOD {lvl}: {dict(counts)} ~grid={grid_size}")

# 读一个瓦片看尺寸
for lvl in sorted(level_data.keys()):
    for face in face_names:
        if face in level_data[lvl]:
            data = zf.read(level_data[lvl][face][0])
            from PIL import Image
            import io
            im = Image.open(io.BytesIO(data))
            print(f"\nTile size: {im.size} (LOD {lvl}, {face})")
            break
    break
