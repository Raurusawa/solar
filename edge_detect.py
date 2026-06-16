import numpy as np
from PIL import Image
import io

img = Image.open("guangyun.png").convert("RGB")
arr = np.array(img, dtype=np.float32)
h, w, _ = arr.shape
print(f"Image: {w}x{h}")

# Grayscale
gray = 0.299 * arr[:,:,0] + 0.587 * arr[:,:,1] + 0.114 * arr[:,:,2]

# Manual Sobel (3x3)
def sobel_x(g):
    return ((g[2:,1:-1] + 2*g[2:,2:] + g[2:,:-2]) -
            (g[:-2,1:-1] + 2*g[:-2,2:] + g[:-2,:-2]))
def sobel_y(g):
    return ((g[1:-1,2:] + 2*g[2:,2:] + g[:-2,2:]) -
            (g[1:-1,:-2] + 2*g[2:,:-2] + g[:-2,:-2]))

gx = np.zeros_like(gray)
gy = np.zeros_like(gray)
gx[1:-1,1:-1] = sobel_x(gray)
gy[1:-1,1:-1] = sobel_y(gray)
edge = np.sqrt(gx**2 + gy**2)

# Center
cy, cx = h//2, w//2

# Find the actual sun center by searching for brightest region
search_r = 100
patch = gray[cy-search_r:cy+search_r, cx-search_r:cx+search_r]
bright_cy, bright_cx = np.unravel_index(np.argmax(patch), patch.shape)
sun_cy = cy - search_r + bright_cy
sun_cx = cx - search_r + bright_cx
print(f"Sun center: ({sun_cx}, {sun_cy})")

# Radial profiles
y, x = np.ogrid[:h, :w]
r = np.sqrt((x - sun_cx)**2 + (y - sun_cy)**2)
max_r = int(min(sun_cx, sun_cy, w-sun_cx, h-sun_cy)) - 1

# Average edge magnitude by radius
edge_by_r = np.zeros(max_r)
gray_by_r = np.zeros(max_r)
for i in range(max_r):
    mask = (r >= i-0.5) & (r < i+0.5)
    cnt = mask.sum()
    if cnt > 0:
        edge_by_r[i] = edge[mask].mean()
        gray_by_r[i] = gray[mask].mean()

# Find edge peaks manually
def find_peaks_manual(data, threshold_ratio=1.5, min_dist=3):
    mean_val = np.mean(data[5:])  # skip center
    threshold = mean_val * threshold_ratio
    peaks = []
    for i in range(1, len(data)-1):
        if data[i] > threshold and data[i] > data[i-1] and data[i] > data[i+1]:
            if not peaks or i - peaks[-1] >= min_dist:
                peaks.append(i)
    return peaks, [data[p] for p in peaks]

edge_peaks, edge_peak_vals = find_peaks_manual(edge_by_r, 1.5, 3)
print(f"\n=== Edge magnitude peaks (radial) ===")
print(f"Baseline mean edge (r>5): {np.mean(edge_by_r[5:]):.3f}")
print(f"Peaks at radii: {edge_peaks}")
print(f"Peak values:    {[f'{v:.3f}' for v in edge_peak_vals]}")

# Analyze gradient of gray (band transitions)
gray_smooth = np.convolve(gray_by_r, np.ones(5)/5, mode='same')
gray_grad = np.gradient(gray_smooth)
grad_peaks, grad_vals = find_peaks_manual(np.abs(gray_grad), 1.8, 3)
print(f"\n=== Gray gradient peaks (radial band transitions) ===")
print(f"Peaks at radii: {grad_peaks}")
print(f"Gradient values: {[f'{v:.3f}' for v in grad_vals]}")

# Radial profile subset
print(f"\n=== Radial gray + edge profile (every 5px up to 200) ===")
print(f"{'r':>4s}  {'gray':>6s}  {'edge':>6s}")
for i in range(0, min(200, max_r), 5):
    marker = " <-" if i in edge_peaks else ""
    print(f"{i:4d}  {gray_by_r[i]:6.1f}  {edge_by_r[i]:6.3f}{marker}")

# Horizontal slice through sun center
print(f"\n=== Horizontal slice edge peaks ===")
h_edge = edge[sun_cy, :]
# Only check right half from center
h_right = h_edge[sun_cx:]
peaks_r, vals_r = find_peaks_manual(h_right, 1.5, 3)
print(f"Right-side peaks (offset from center): {peaks_r}")
print(f"Values: {[f'{v:.3f}' for v in vals_r]}")

print(f"\n=== Diagnosis ===")
n_bands = len(edge_peaks)
print(f"Number of visible edge rings: {n_bands}")
if n_bands > 0:
    print(f"Ring radii: {edge_peaks}")
    print(f"This confirms visible banding in the lens flare glow")
