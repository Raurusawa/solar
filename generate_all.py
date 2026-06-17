"""Batch generate equirect textures for all planet PAK files.
Pipeline: Earth step3 baseline + neg_y ROTATE_180
"""
import zipfile, os, math, io
import numpy as np
from PIL import Image
from collections import defaultdict

PAK_DIR = r'c:\Users\nico_\Desktop\solar\textures\textures\planets'
OUT_DIR = r'c:\Users\nico_\Desktop\solar\textures'
CUBE = ['pos_x','neg_x','pos_y','neg_y','pos_z','neg_z']
SIDE = ['pos_x','neg_x','pos_z','neg_z']
EQUIRECT_W, EQUIRECT_H = 4096, 2048
SKIP_PLANETS = {'Rings'}  # Rings.pak is not a planet surface

def sample_face_bilinear(farr, f, u, v, fsz):
    """Bilinear sample from cubemap face, with coordinates u,v in [0,1]."""
    u = u * fsz - 0.5
    v = (1.0 - v) * fsz - 0.5  # flip V: image Y goes down, UV Y goes up
    x0 = int(math.floor(u)); y0 = int(math.floor(v))
    x1 = x0 + 1; y1 = y0 + 1
    fx = u - x0; fy = v - y0
    # Clamp to [0, fsz-1]
    fm1 = int(fsz) - 1
    x0 = max(0, min(fm1, x0)); x1 = max(0, min(fm1, x1))
    y0 = max(0, min(fm1, y0)); y1 = max(0, min(fm1, y1))
    fa = farr[f]
    return ((fa[y0, x0] * (1-fx) + fa[y0, x1] * fx) * (1-fy) +
            (fa[y1, x0] * (1-fx) + fa[y1, x1] * fx) * fy)

def cubemap_to_equirect(face_imgs, ew, eh):
    eq = np.zeros((eh, ew, 3), dtype=np.float32)
    farr = {}
    fsz = None
    for fn, img in face_imgs.items():
        farr[fn] = np.array(img, dtype=np.float32)
        if fsz is None: fsz = img.size[0]
    fs = float(fsz)
    for py in range(eh):
        lat = (0.5 - py/eh) * math.pi
        cl = math.cos(lat); sl = math.sin(lat)
        for px in range(ew):
            lon = (px/ew - 0.5) * 2 * math.pi
            dx = math.cos(lon)*cl; dy = sl; dz = math.sin(lon)*cl
            adx, ady, adz = abs(dx), abs(dy), abs(dz)
            if adx >= ady and adx >= adz:
                f, s, t = ('pos_x', -dz/adx, -dy/adx) if dx > 0 else ('neg_x', dz/adx, -dy/adx)
            elif ady >= adx and ady >= adz:
                f, s, t = ('pos_y', dx/ady, dz/ady) if dy > 0 else ('neg_y', dx/ady, -dz/ady)
            else:
                f, s, t = ('pos_z', dx/adz, -dy/adz) if dz > 0 else ('neg_z', -dx/adz, -dy/adz)
            if f not in farr: continue
            u = (s+1)*0.5; v = (t+1)*0.5
            eq[py, px] = sample_face_bilinear(farr, f, u, v, fs)
    return eq

def process_pak(pak_path, planet_name):
    zf = zipfile.ZipFile(pak_path)
    all_names = [n for n in zf.namelist() if n.endswith('.jpg') or n.endswith('.png')]

    # Find all LOD levels per face
    level_data = defaultdict(lambda: defaultdict(list))
    for n in all_names:
        parts = n.strip('/').split('/')
        face = None
        for p in parts:
            if p in CUBE:
                face = p
                break
        if not face:
            continue
        fn_base = parts[-1].rsplit('.', 1)[0]  # e.g. "5_0_0_c" or "5_0_0"
        s = fn_base.split('_')
        try:
            lvl = int(s[0])
        except ValueError:
            continue
        level_data[lvl][face].append(n)

    if not level_data:
        zf.close()
        return None

    # Pick highest LOD that has all 6 faces
    valid_levels = [l for l in level_data if len(level_data[l]) == 6]
    if not valid_levels:
        zf.close()
        return None
    best = max(valid_levels)
    tiles = level_data[best]

    # For Earth, there are _a and _c variants. Filter to use _c only.
    # Check if any tile has _c suffix
    first_tile = list(tiles.values())[0][0]
    if '_c.' in first_tile or first_tile.endswith('_c.jpg'):
        # Filter to _c suffix only
        for fn in tiles:
            tiles[fn] = [t for t in tiles[fn] if '_c.' in t or t.endswith('_c.jpg')]

    # Get tile size
    first_tiles = tiles[list(tiles.keys())[0]]
    if not first_tiles:
        zf.close()
        return None
    fw = Image.open(io.BytesIO(zf.read(first_tiles[0]))).size[0]

    # Build grids
    grids = {}
    for fn in CUBE:
        if fn not in tiles or not tiles[fn]:
            zf.close()
            return None
        tmap = {}
        xs, ys = set(), set()
        for name in tiles[fn]:
            fn_base = name.strip('/').split('/')[-1].rsplit('.', 1)[0]
            s = fn_base.split('_')
            try:
                x, y = int(s[1]), int(s[2])
            except (ValueError, IndexError):
                continue
            xs.add(x); ys.add(y)
            tmap[(x, y)] = name
        if not xs or not ys:
            zf.close()
            return None
        minx, maxx = min(xs), max(xs)
        miny, maxy = min(ys), max(ys)
        gw = maxx - minx + 1
        gh = maxy - miny + 1
        nmap = {}
        for (x, y), name in tmap.items():
            nmap[(x - minx, maxy - y)] = name
        grids[fn] = (nmap, gw, gh)

    min_grid = min(gh for _, _, gh in grids.values())
    fres = min_grid * fw

    def build_face(nmap, gw, gh):
        W, H = gw * fw, gh * fw
        face = Image.new('RGB', (W, H))
        for (x, y), name in nmap.items():
            img = Image.open(io.BytesIO(zf.read(name))).convert('RGB')
            # Step 1: tile ROTATE_90 CCW
            img = img.transpose(Image.ROTATE_90)
            face.paste(img, (x * fw, y * fw))
        return face.resize((fres, fres), Image.LANCZOS)

    # Build faces
    face_imgs = {}
    for fn in CUBE:
        nmap, gw, gh = grids[fn]
        face_imgs[fn] = build_face(nmap, gw, gh)

    # Step 2: side faces ROTATE_90 CCW
    for fn in SIDE:
        face_imgs[fn] = face_imgs[fn].transpose(Image.ROTATE_90)

    # Step 3: swap pos_x<->pos_z, neg_x<->neg_z
    face_imgs['pos_x'], face_imgs['pos_z'] = face_imgs['pos_z'], face_imgs['pos_x']
    face_imgs['neg_x'], face_imgs['neg_z'] = face_imgs['neg_z'], face_imgs['neg_x']

    # neg_y clockwise 180
    face_imgs['neg_y'] = face_imgs['neg_y'].transpose(Image.ROTATE_180)

    # Generate equirect
    eq = cubemap_to_equirect(face_imgs, EQUIRECT_W, EQUIRECT_H)
    result = Image.fromarray(eq.clip(0, 255).astype(np.uint8))

    zf.close()
    return result

def main():
    pak_files = [f for f in os.listdir(PAK_DIR) if f.endswith('.pak')]
    success, failed = 0, 0

    for pak_name in sorted(pak_files):
        planet = pak_name.split('-')[0]
        if planet in SKIP_PLANETS:
            continue
        pak_path = os.path.join(PAK_DIR, pak_name)
        print(f'{planet}: ', end='', flush=True)

        try:
            result = process_pak(pak_path, planet)
            if result is None:
                print('SKIP (no valid faces)')
                continue
            # Save as {planet}.jpg (lowercase, matches config.ini texturePath)
            out_name = planet.lower() + '.jpg'
            out_path = os.path.join(OUT_DIR, out_name)
            result.save(out_path, 'JPEG', quality=93)
            print(f'OK -> {out_name} ({result.size[0]}x{result.size[1]})')
            success += 1
        except Exception as e:
            print(f'FAIL: {e}')
            failed += 1
            import traceback
            traceback.print_exc()

    print(f'\nDone: {success} success, {failed} failed')

if __name__ == '__main__':
    main()
