import os
import math
from PIL import Image, ImageDraw, ImageFilter

# 8x8 Bayer Ordered Dithering 矩阵
# 用于生成非常对称、规律、极具复古液晶/Gameboy点阵质感的像素抖动
BAYER_8x8 = [
    [ 0, 48, 12, 60,  3, 51, 15, 63],
    [32, 16, 44, 28, 35, 19, 47, 31],
    [ 8, 56,  4, 52, 11, 59,  7, 55],
    [40, 24, 36, 20, 43, 27, 39, 23],
    [ 2, 50, 14, 62,  1, 49, 13, 61],
    [34, 18, 46, 30, 33, 17, 45, 29],
    [10, 58,  6, 54,  9, 57,  5, 53],
    [42, 26, 38, 22, 41, 25, 37, 21]
]

def apply_bayer_dither(gray_img):
    """
    对灰度图应用 8x8 Bayer 有序抖动算法，生成完美的 1-bit 像素图
    """
    width, height = gray_img.size
    dithered = Image.new('1', (width, height))
    
    gray_pixels = gray_img.load()
    dithered_pixels = dithered.load()
    
    for y in range(height):
        for x in range(width):
            gray_val = gray_pixels[x, y]
            threshold = int((BAYER_8x8[y % 8][x % 8] + 0.5) * 4)
            
            if gray_val >= threshold:
                dithered_pixels[x, y] = 1
            else:
                dithered_pixels[x, y] = 0
                
    return dithered

def colorize_1bit_with_mask(dithered_img, mask_img, fg_color, bg_color, ext_color=(0,0,0)):
    """
    将 1-bit 图像着色，同时根据 mask_img 区分圆角面板内部和外部。
    面板内部采用 fg_color (前景色) 和 bg_color (背景色)；
    面板外部采用 ext_color (通常为全黑或透明底色)。
    """
    width, height = dithered_img.size
    colored = Image.new('RGB', (width, height), ext_color)
    
    pixels = dithered_img.load()
    mask_pixels = mask_img.load()
    
    for y in range(height):
        for x in range(width):
            if mask_pixels[x, y] == 1:
                # 面板内部：根据抖动值选用前景或背景色
                if pixels[x, y] == 1:
                    colored.putpixel((x, y), fg_color)
                else:
                    colored.putpixel((x, y), bg_color)
            else:
                # 面板外部：使用外围底色
                colored.putpixel((x, y), ext_color)
                
    return colored

def main():
    print("--- Mstudio Retro Pixel Logo Generator (White Background Optimization) ---")
    size = 256
    
    # 1. 定义圆角面板底板的几何边界 (留出 12 像素的外边距)
    panel_rect = [(12, 12), (244, 244)]
    panel_radius = 24
    
    # 创建圆角面板的掩膜 (内部为 1，外部为 0)
    mask_layer = Image.new('1', (size, size), 0)
    mask_draw = ImageDraw.Draw(mask_layer)
    mask_draw.rounded_rectangle(panel_rect, radius=panel_radius, fill=1)
    
    # 2. 创建高精度的灰度主画布
    canvas = Image.new('L', (size, size), 0)
    draw = ImageDraw.Draw(canvas)
    
    # 面板底色填为轻微的暗灰 (35)，这在抖动后会产生极佳的复古液晶颗粒质感
    # 在掩膜内部填充底色
    for y in range(size):
        for x in range(size):
            if mask_layer.getpixel((x, y)) == 1:
                canvas.putpixel((x, y), 35)
                
    # 3. 绘制背景微弱的示波器格线 (频率 32 像素)
    for i in range(32, size - 16, 32):
        for pos in range(16, size - 16, 4):
            # 纵线 (仅在面板掩膜内绘制)
            if mask_layer.getpixel((i, pos)) == 1:
                canvas.putpixel((i, pos), 65)
            # 横线
            if mask_layer.getpixel((pos, i)) == 1:
                canvas.putpixel((pos, i), 65)
            
    # 4. 绘制示波器阻尼波形 (横穿面板，极具科技感)
    wave_pixels = []
    for x in range(size):
        decay = math.exp(-((x - size/2) / 80) ** 2)
        y = 128 + 44 * math.sin(x * 0.08) * decay
        wave_pixels.append((x, int(y)))
        
    for i in range(len(wave_pixels) - 1):
        pt1, pt2 = wave_pixels[i], wave_pixels[i+1]
        # 仅当两个端点都在面板内时绘制
        if mask_layer.getpixel(pt1) == 1 and mask_layer.getpixel(pt2) == 1:
            draw.line([pt1, pt2], fill=160, width=2)
        
    # 5. 绘制对称的硬朗像素 M 字母主体
    # 我们定义 M 的精确像素顶点，保证完全对称与极佳的视觉平衡
    left_rect = [(44, 64), (80, 192)]
    right_rect = [(176, 64), (212, 192)]
    v_poly = [
        (80, 64),    # 左外顶点
        (128, 140),  # 中底外顶点
        (176, 64),   # 右外顶点
        (176, 96),   # 右内角
        (128, 172),  # 中底内角
        (80, 96)     # 左内角
    ]
    
    # 绘制影子层以生成精致的点阵渐变发光阴影
    shadow_canvas = Image.new('L', (size, size), 0)
    shadow_draw = ImageDraw.Draw(shadow_canvas)
    
    offset_dx, offset_dy = 8, 8
    
    # 在影子上绘制偏移的 M
    shadow_draw.rectangle([(left_rect[0][0] + offset_dx, left_rect[0][1] + offset_dy),
                           (left_rect[1][0] + offset_dx, left_rect[1][1] + offset_dy)], fill=110)
    shadow_draw.rectangle([(right_rect[0][0] + offset_dx, right_rect[0][1] + offset_dy),
                           (right_rect[1][0] + offset_dx, right_rect[1][1] + offset_dy)], fill=110)
    offset_v_poly = [(pt[0] + offset_dx, pt[1] + offset_dy) for pt in v_poly]
    shadow_draw.polygon(offset_v_poly, fill=110)
    
    # 对影子层进行模糊处理
    blurred_shadow = shadow_canvas.filter(ImageFilter.GaussianBlur(radius=8))
    
    # 在主画布上绘制白色 (255) 实体 M 字母
    draw.rectangle(left_rect, fill=255)
    draw.rectangle(right_rect, fill=255)
    draw.polygon(v_poly, fill=255)
    
    # 将主体与模糊影子进行 Max 混合
    final_gray = Image.new('L', (size, size), 0)
    canvas_pixels = canvas.load()
    shadow_pixels = blurred_shadow.load()
    final_pixels = final_gray.load()
    
    for y in range(size):
        for x in range(size):
            if mask_layer.getpixel((x, y)) == 1:
                final_pixels[x, y] = max(canvas_pixels[x, y], int(shadow_pixels[x, y] * 0.85))
            else:
                final_pixels[x, y] = 0
                
    # 6. 在最外圈画一个漂亮的像素显示屏白色描边框 (255 亮白)
    # 这确保了在白色和深色背景下都有极其漂亮的物理边界
    draw.rounded_rectangle(panel_rect, radius=panel_radius, outline=255, width=4)
    # 重新把圆角框的线合并到 final_gray 中
    ImageDraw.Draw(final_gray).rounded_rectangle(panel_rect, radius=panel_radius, outline=255, width=4)
            
    # 7. 应用 8x8 Bayer Dither 算法生成完美的 1-bit 图标
    dithered_1bit = apply_bayer_dither(final_gray)
    
    # 8. 生成高对比度、可在任意背景下完美识别的着色版本
    # A. 经典的黑白极简版本 (含有深色底板 #1a1a1a，外围透明)
    # 这样在白色资源管理器下，会显示一个深灰色的圆角矩形，里面镶嵌着白色 M 字母，对比度极高！
    logo_bw = colorize_1bit_with_mask(dithered_1bit, mask_layer, (255, 255, 255), (20, 20, 20), (0, 0, 0))
    logo_bw.save("logo_bw.png")
    print("[SUCCESS] Generated logo_bw.png with Dark Panel Plate!")
    
    # B. 骚气的 Gameboy 墨绿版本 (面板内为 GB 经典调色，外围全黑)
    logo_gb = colorize_1bit_with_mask(dithered_1bit, mask_layer, (15, 56, 15), (155, 188, 15), (0, 0, 0))
    logo_gb.save("logo_gameboy.png")
    print("[SUCCESS] Generated logo_gameboy.png with Gameboy Screen Style!")
    
    # 9. 打包生成多分辨率打包的 Windows ico 图标
    # 我们根据掩膜来实现面板外的完全透明 (Alpha=0)，面板内的完全不透明 (Alpha=255)
    icon_rgba = Image.new('RGBA', (size, size))
    bw_pixels = logo_bw.load()
    rgba_pixels = icon_rgba.load()
    mask_pixels = mask_layer.load()
    
    for y in range(size):
        for x in range(size):
            if mask_pixels[x, y] == 1:
                # 面板内部完全不透明，颜色取自 logo_bw
                r, g, b = bw_pixels[x, y]
                rgba_pixels[x, y] = (r, g, b, 255)
            else:
                # 面板外部完全透明
                rgba_pixels[x, y] = (0, 0, 0, 0)
                
    # 打包保存为多尺寸的图标文件
    icon_rgba.save("mstudio.ico", format="ICO", sizes=[
        (16, 16),
        (32, 32),
        (48, 48),
        (64, 64),
        (128, 128),
        (256, 256)
    ])
    print("[SUCCESS] Generated multi-size mstudio.ico with transparent round corners!")

if __name__ == "__main__":
    main()
