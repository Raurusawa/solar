#!/usr/bin/env python3
"""
SpaceEngine PAK → Equirectangular JPG 提取 (v9 方向修正版)
修正方案（针对 Earth-Surface-PBC.pak 验证通过）：
1. 每瓦片旋转 270°
2. 面拼图整体旋转 270°
3. 每瓦片行高的横条上下翻转
4. 面整体水平翻转
5. 四个侧面 (pos_x, neg_x, pos_z, neg_z)：额外上下翻转 + 水平翻转
6. pos_z ↔ neg_z 交换
"""
import zipfile, os, sys, math, io, json
from collections import defaultdict
from PIL import Image
import numpy as np
import argparse

PLANETS_DIR = r"c:\Users\nico_\Desktop\solar\textures\textures\planets"
OUTPUT_DIR  = r"c:\Users\nico_\Desktop\solar\textures"

ALL_TARGETS = {
    "mercury": ("Mercury-Surface-HD.pak",     2048),
    "venus":   ("Venus-Surface-JVV.pak",       2048),
    "earth":   ("Earth-Surface-PBC.pak",       4096),
    "mars":    ("Mars-Surface-PBC.pak",        2048),
    "jupiter": ("Jupiter-Surface-BJ.pak",      2048),
    "saturn":  ("Saturn-Surface-SSS.pak",      2048),
    "uranus":  ("Uranus-Surface-SF.pak",       1024),
    "neptune": ("Neptune-Surface-SF.pak",      1024),
    # 卫星
    "earth_moon":      ("Moon-Surface-KL.pak",       2048),
    "jupiter_io":      ("Io-Surface-PBC.pak",        1024),
    "jupiter_europa":  ("Europa-Surface-BJ.pak",     1024),
    "jupiter_ganymede":("Ganymede-Surface-PBC.pak",  1024),
    "jupiter_callisto":("Callisto-Surface-JVV.pak",  1024),
    "saturn_titan":    ("Titan-Surface-RV.pak",      2048),
    "saturn_rhea":     ("Rhea-Surface-PBC.pak",      1024),
    "saturn_dione":    ("Dione-Surface-PBC.pak",     1024),
    "saturn_iapetus":  ("Iapetus-Surface-KX.pak",    1024),
    "uranus_titania":  ("Titania-Surface-SF.pak",    1024),
    "uranus_oberon":   ("Oberon-Surface-SF.pak",     1024),
    "neptune_triton":  ("Triton-Surface-SE.pak",     1024),
    "mars_phobos":     ("Phobos-Surface-PS.pak",      512),
    "mars_deimos":     ("Deimos-Surface-PBC.pak",     512),
    "saturn_enceladus":("Enceladus-Surface-PBC.pak",  1024),
    "saturn_tethys":   ("Tethys-Surface-PBC.pak",     1024),
    "saturn_mimas":    ("Mimas-Surface-KX.pak",       1024),
    "uranus_miranda":  ("Miranda-Surface-SF.pak",     1024),
    "uranus_ariel":    ("Ariel-Surface-SF.pak",       1024),
    "uranus_umbriel":  ("Umbriel-Surface-SF.pak",     1024),
}

CUBE_FACES = ['pos_x', 'neg_x', 'pos_y', 'neg_y', 'pos_z', 'neg_z']
SIDE_FACES = ['pos_x', 'neg_x', 'pos_z', 'neg_z']  # 侧面（需要额外变换）

def find_best_complete_level(zf):
    """找含有 6 面的最高 LOD 级别，只取 _c 后缀 (RGB颜色通道)"""
    jpgs = [n for n in zf.namelist() 
            if (n.endswith('.jpg') or n.endswith('.png')) 
            and 'base.' not in n.lower().split('/')[-1]]
    
    # 检测是否是多通道PAK (_a/_c 后缀)
    has_suffix = any('_a.' in n or '_c.' in n for n in jpgs)
    
    level_data = defaultdict(lambda: defaultdict(dict))
    for n in jpgs:
        parts = n.strip('/').split('/')
        face = None
        for p in parts:
            if p in CUBE_FACES:
                face = p
                break
        if not face:
            continue
        tile_name = parts[-1].rsplit('.', 1)[0]
        s = tile_name.split('_')
        if len(s) < 3:
            continue
        try:
            lvl = int(s[0])
            x, y = int(s[1]), int(s[2])
        except:
            continue
        suffix = s[3] if len(s) >= 4 else ''
        
        # 多通道PAK：只取 _c (RGB颜色)
        if has_suffix and suffix != 'c':
            continue
        
        # 去重：同一 (level, face, x, y) 只保留最后一个
        level_data[lvl][face][(x, y)] = n
    
    # 转为旧格式 [# of tiles per face]
    result = {}
    for lvl, faces in level_data.items():
        result[lvl] = {fn: list(d.values()) for fn, d in faces.items()}
    
    # 找最高完整的 level
    best = -1
    for lvl in sorted(result.keys(), reverse=True):
        if len(result[lvl]) >= 6:
            best = lvl
            break
    if best == -1:
        return None, None
    return best, result[best]


def build_face_image(zf, tiles, target_size=None):
    """
    拼接一个面的所有瓦片，并应用 v9 方向修正：
    1. 每瓦片旋转 270° 后粘贴
    2. 拼图完成后整体旋转 270°
    3. 每瓦片行高的横条上下翻转
    4. 整体水平翻转
    """
    xs = set()
    ys = set()
    for name in tiles:
        parts = name.strip('/').split('/')
        tile_name = parts[-1].rsplit('.', 1)[0]
        s = tile_name.split('_')
        x, y = int(s[1]), int(s[2])
        xs.add(x)
        ys.add(y)
    
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    grid_w = max_x - min_x + 1
    grid_h = max_y - min_y + 1
    
    # 读一个瓦片看尺寸
    data = zf.read(tiles[0])
    img0 = Image.open(io.BytesIO(data))
    tile_w, tile_h = img0.size
    
    face_w = grid_w * tile_w
    face_h = grid_h * tile_h
    face_img = Image.new('RGB', (face_w, face_h), (0, 0, 0))
    
    # 构建 (x,y) → name 映射
    tile_map = {}
    for name in tiles:
        parts = name.strip('/').split('/')
        tile_name = parts[-1].rsplit('.', 1)[0]
        s = tile_name.split('_')
        x, y = int(s[1]), int(s[2])
        tile_map[(x, y)] = name
    
    for (x, y), name in tile_map.items():
        data = zf.read(name)
        img = Image.open(io.BytesIO(data))
        if img.mode != 'RGB':
            img = img.convert('RGB')
        
        # 1. 每瓦片旋转 270° (ROTATE_270 = 逆时针90°)
        img = img.transpose(Image.ROTATE_270)
        
        px = (x - min_x) * tile_w
        # Y翻转: SE约定 y增加=向上, PIL y增加=向下
        py = (grid_h - 1 - (y - min_y)) * tile_h
        face_img.paste(img, (px, py))
    
    # 2. 整体旋转 270°
    face_img = face_img.transpose(Image.ROTATE_270)
    
    # 3. 每瓦片行高的横条上下翻转 (原始分辨率，strip 边界精确对齐瓦片行)
    #    旋转后 face 的 height = 原始 grid_w * tile_w，每行 strip 高度 = tile_h
    arr = np.array(face_img)
    strip_h = tile_h  # 原始瓦片高度，旋转后变成 strip 高度
    for row in range(grid_h):
        y0 = row * strip_h
        y1 = y0 + strip_h
        arr[y0:y1] = arr[y0:y1][::-1]  # 上下翻转
    
    face_img = Image.fromarray(arr)
    
    # 4. 整体水平翻转
    face_img = face_img.transpose(Image.FLIP_LEFT_RIGHT)
    
    # 5. 最后才缩放到目标尺寸
    if target_size:
        face_img = face_img.resize((target_size, target_size), Image.LANCZOS)
    
    return face_img, tile_w, tile_h, grid_w, grid_h


def cubemap_to_equirect(face_imgs, eq_w, eq_h):
    """6面立方体 → equirectangular (标准映射)"""
    eq = np.zeros((eq_h, eq_w, 3), dtype=np.float32)
    
    face_arr = {}
    face_size = None
    for fn, img in face_imgs.items():
        face_arr[fn] = np.array(img, dtype=np.float32)
        if face_size is None:
            face_size = img.size[0]
    
    fs = face_size - 1
    
    for py in range(eq_h):
        lat = (0.5 - py / eq_h) * math.pi
        cos_lat = math.cos(lat)
        sin_lat = math.sin(lat)
        
        for px in range(eq_w):
            lon = (px / eq_w - 0.5) * 2 * math.pi
            
            dx = math.cos(lon) * cos_lat
            dy = sin_lat
            dz = math.sin(lon) * cos_lat
            
            adx, ady, adz = abs(dx), abs(dy), abs(dz)
            
            if adx >= ady and adx >= adz:
                if dx > 0:
                    face, s, t = 'pos_x', -dz/adx, -dy/adx
                else:
                    face, s, t = 'neg_x', dz/adx, -dy/adx
            elif ady >= adx and ady >= adz:
                if dy > 0:
                    face, s, t = 'pos_y', dx/ady, dz/ady
                else:
                    face, s, t = 'neg_y', dx/ady, -dz/ady
            else:
                if dz > 0:
                    face, s, t = 'pos_z', dx/adz, -dy/adz
                else:
                    face, s, t = 'neg_z', -dx/adz, -dy/adz
            
            if face not in face_arr:
                continue
            
            # (s,t) ∈ [-1,1]² → uv ∈ [0,1]²
            u = (s + 1) * 0.5
            v = (t + 1) * 0.5
            
            col = int(u * fs + 0.5)
            row = int((1 - v) * fs + 0.5)
            
            col = max(0, min(fs, col))
            row = max(0, min(fs, row))
            
            eq[py, px] = face_arr[face][row, col]
    
    return eq


def process_pak(pak_path, output_path, eq_w=2048, name=""):
    if not os.path.exists(pak_path):
        return False, "PAK not found"
    
    with zipfile.ZipFile(pak_path, 'r') as zf:
        best_level, face_tiles = find_best_complete_level(zf)
        if best_level is None:
            return False, "no complete 6-face level"
        
        total_tiles = sum(len(t) for t in face_tiles.values())
        
        # 读取一个瓦片获取尺寸
        any_face = next(iter(face_tiles.keys()))
        first_tile = face_tiles[any_face][0]
        data = zf.read(first_tile)
        img = Image.open(io.BytesIO(data))
        tile_w = img.size[0]
        
        # 计算每个面的网格尺寸
        grid_sizes = {}
        for fn in CUBE_FACES:
            if fn not in face_tiles:
                return False, f"missing face: {fn}"
            name_to_xy = {}
            for tname in face_tiles[fn]:
                parts = tname.strip('/').split('/')
                tile_name = parts[-1].rsplit('.', 1)[0]
                s = tile_name.split('_')
                x, y = int(s[1]), int(s[2])
                name_to_xy[tname] = (x, y)
            xs = set(x for x,_ in name_to_xy.values())
            ys = set(y for _,y in name_to_xy.values())
            grid_w = max(xs) - min(xs) + 1
            grid_h = max(ys) - min(ys) + 1
            grid_sizes[fn] = (grid_w, grid_h)
        
        # 统一到最小的 grid 尺寸
        min_grid = min(max(gw, gh) for gw, gh in grid_sizes.values())
        face_res = min_grid * tile_w
        
        # 构建6面（含 v9 方向修正：旋转+条带翻转+水平翻转）
        face_imgs = {}
        for fn in CUBE_FACES:
            result = build_face_image(zf, face_tiles[fn], face_res)
            if result is None:
                return False, f"build face {fn} failed"
            face_imgs[fn] = result[0]
        
        # v9 侧面额外变换 + pos_z/neg_z 交换
        import copy
        side_processed = {}
        for fn in SIDE_FACES:
            if fn in face_imgs:
                img = face_imgs[fn]
                # 5a. 上下翻转
                img = img.transpose(Image.FLIP_TOP_BOTTOM)
                # 5b. 水平翻转
                img = img.transpose(Image.FLIP_LEFT_RIGHT)
                side_processed[fn] = img
        
        # 替换回 face_imgs
        for fn in SIDE_FACES:
            if fn in side_processed:
                face_imgs[fn] = side_processed[fn]
        
        # 6. 交换 pos_z ↔ neg_z
        if 'pos_z' in face_imgs and 'neg_z' in face_imgs:
            face_imgs['pos_z'], face_imgs['neg_z'] = face_imgs['neg_z'], face_imgs['pos_z']
        
        # 转换为 equirectangular
        eq_h = eq_w // 2
        eq = cubemap_to_equirect(face_imgs, eq_w, eq_h)
        
        img = Image.fromarray(eq.clip(0, 255).astype(np.uint8))
        os.makedirs(os.path.dirname(output_path), exist_ok=True)
        img.save(output_path, 'JPEG', quality=93)
        
        return True, f"LOD{best_level} {eq_w}x{eq_h} (face:{face_res}px, tiles:{total_tiles})"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--planet', help='行星名')
    parser.add_argument('--all', action='store_true', help='处理全部')
    parser.add_argument('--res', type=int, default=0, help='输出宽度 (0=自动)')
    args = parser.parse_args()
    
    if args.planet and args.planet in ALL_TARGETS:
        targets = {args.planet: ALL_TARGETS[args.planet]}
    elif args.all:
        targets = ALL_TARGETS
    else:
        print("用法: --all (全部) 或 --planet earth")
        print("可用: " + ", ".join(ALL_TARGETS.keys()))
        return
    
    for name, (pak, default_res) in targets.items():
        eq_w = args.res if args.res > 0 else default_res
        pak_path = os.path.join(PLANETS_DIR, pak)
        output = os.path.join(OUTPUT_DIR, f"{name}.jpg")
        
        print(f"\n{name} ({pak}) ", end="")
        ok, msg = process_pak(pak_path, output, eq_w, name)
        if ok:
            sz = os.path.getsize(output)
            print(f"OK {msg} -> {sz/1024:.0f}KB")
        else:
            print(f"FAIL {msg}")


if __name__ == '__main__':
    main()
