from PIL import Image
import numpy as np

img = Image.open(r'c:\Users\nico_\Desktop\solar\screenshot4.png')
arr = np.array(img)
h, w = arr.shape[:2]
print(f"Full screenshot: {w}x{h}")

# 逐行找暗色行（OpenGL背景通常是纯黑）
print("\nRows with >80% dark pixels (OpenGL window region):")
dark_rows = []
for y in range(h):
    dark = (arr[y,:,:].max(axis=1) < 20).sum()
    if dark > w * 0.8:
        dark_rows.append(y)

if dark_rows:
    # 找连续区间
    breaks = [i for i in range(1, len(dark_rows)) if dark_rows[i] - dark_rows[i-1] > 1]
    regions = []
    start = dark_rows[0]
    for b in breaks:
        regions.append((start, dark_rows[b-1]))
        start = dark_rows[b]
    regions.append((start, dark_rows[-1]))
    for s, e in regions:
        print(f"  Dark region: rows {s}-{e} ({e-s+1} rows)")
else:
    print("  No dark rows found. Screen might be outside window.")

# 找亮色行
print("\nRows with >80% bright pixels:")
for y in range(0, h, 50):
    bright = ((arr[y,:,0] > 200) & (arr[y,:,1] > 200) & (arr[y,:,2] > 200)).sum()
    if bright > w * 0.8:
        print(f"  Row {y}: {bright}/{w} bright")

# 垂直方向颜色剖面
print("\nVertical profile (col center, every 20 rows):")
col_center = arr[:, w//2, :]
for y in range(0, h, 20):
    r, g, b = col_center[y, 0], col_center[y, 1], col_center[y, 2]
    tag = ""
    if r < 20 and g < 20 and b < 20: tag = " DARK"
    elif r > 230 and g > 230 and b > 230: tag = " WHITE"
    elif r > 200 and g > 150 and b < 50: tag = " GOLD"
    print(f"  y={y:4d}: RGB({r:3d},{g:3d},{b:3d}){tag}")

# 水平方向颜色剖面（中间行）
print(f"\nHorizontal profile (row {h//2}, every 30 cols):")
mid_row = arr[h//2, :, :]
for x in range(0, w, 30):
    r, g, b = mid_row[x, 0], mid_row[x, 1], mid_row[x, 2]
    tag = ""
    if r < 20 and g < 20 and b < 20: tag = " DARK"
    elif r > 230 and g > 230 and b > 230: tag = " WHITE"
    elif r > 200 and g > 150 and b < 50: tag = " GOLD"
    print(f"  x={x:4d}: RGB({r:3d},{g:3d},{b:3d}){tag}")
