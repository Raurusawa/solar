from PIL import Image
import numpy as np
img = Image.open(r'c:\Users\nico_\Desktop\solar\direct.png')
arr = np.array(img)
h, w = arr.shape[:2]
print(f"Size: {w}x{h}")

# 垂直剖面
print("\nVertical center profile (every 20 rows):")
col = arr[:, w//2, :]
for y in range(0, h, 20):
    r,g,b = col[y]
    tag = ""
    if r<20 and g<20 and b<20: tag=" DARK"
    elif r+g+b>700: tag=" BRIGHT"
    elif r>200 and g>150 and b<50: tag=" GOLD"
    print(f"  y={y:4d}: ({r:3d},{g:3d},{b:3d}){tag}")

# 水平剖面
print(f"\nHorizontal center profile (every 30 cols):")
mid = arr[h//2, :, :]
for x in range(0, w, 30):
    r,g,b = mid[x]
    tag = ""
    if r<20 and g<20 and b<20: tag=" DARK"
    elif r+g+b>700: tag=" BRIGHT"
    elif r>200 and g>150 and b<50: tag=" GOLD"
    print(f"  x={x:4d}: ({r:3d},{g:3d},{b:3d}){tag}")

# 统计
flat = arr.reshape(-1,3)
white = (flat[:,0]>240)&(flat[:,1]>240)&(flat[:,2]>240)
dark  = flat.max(axis=1)<30
print(f"\nWhite: {white.sum()/len(flat)*100:.1f}%")
print(f"Dark:  {dark.sum()/len(flat)*100:.1f}%")
