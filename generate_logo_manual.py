import os

# Configuration
WIDTH = 199  # Matches original file header
HEIGHT = 60
FILE_PATH = "src/logo_aiplan.c"

# Bitmap Font (Simple Block Font 10x14)
# 1 = Pixel On, 0 = Off
CHARS = {
    'A': [
        "0011100",
        "0110110",
        "1100011",
        "1100011",
        "1111111",
        "1100011",
        "1100011",
        "1100011",
        "1100011",
        "1100011",
    ],
    'I': [
        "1111111",
        "0011100",
        "0011100",
        "0011100",
        "0011100",
        "0011100",
        "0011100",
        "0011100",
        "0011100",
        "1111111",
    ],
    'P': [
        "1111100",
        "1100110",
        "1100011",
        "1100011",
        "1100110",
        "1111100",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
    ],
    'L': [
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1100000",
        "1111111",
    ],
    'N': [
        "1100001",
        "1110001",
        "1110001",
        "1101001",
        "1101001",
        "1100101",
        "1100101",
        "1100011",
        "1100011",
        "1100011",
    ]
}

def create_canvas(w, h):
    # Returns list of lists (rows) of 0s
    return [[0 for _ in range(w)] for _ in range(h)]

def draw_char(canvas, char_key, x_offset, y_offset, scale=2):
    if char_key not in CHARS:
        return x_offset
        
    matrix = CHARS[char_key]
    rows = len(matrix)
    cols = len(matrix[0])
    
    for r in range(rows):
        for c in range(cols):
            if matrix[r][c] == '1':
                # Scale up
                for sr in range(scale):
                    for sc in range(scale):
                        px = x_offset + c * scale + sc
                        py = y_offset + r * scale + sr
                        if 0 <= py < len(canvas) and 0 <= px < len(canvas[0]):
                            canvas[py][px] = 1
    
    return x_offset + (cols * scale) + (2 * scale) # Advance cursor + padding

def generate_logo_data():
    canvas = create_canvas(WIDTH, HEIGHT)
    
    text = "AIPLAN"
    start_x = 10
    start_y = 10
    scale = 4 # Make it big
    
    cursor_x = start_x
    for char in text:
        cursor_x = draw_char(canvas, char, cursor_x, start_y, scale)
        
    # Convert to LVGL 1-bit Buffer
    # Row-major, packed bytes. MSB first.
    # Stride = (WIDTH + 7) // 8 bytes per row
    
    stride = (WIDTH + 7) // 8
    total_bytes = stride * HEIGHT
    byte_array = []
    
    for y in range(HEIGHT):
        row_bytes = []
        current_byte = 0
        bit_idx = 0
        
        for x in range(WIDTH):
            bit = canvas[y][x]
            # LVGL ALPHA_1BIT: MSB is pixel 0
            # If bit_idx is 0 (MSB), we OR with 0x80
            shift = 7 - bit_idx
            if bit:
                current_byte |= (1 << shift)
                
            bit_idx += 1
            if bit_idx == 8:
                row_bytes.append(current_byte)
                current_byte = 0
                bit_idx = 0
        
        # Flush remaining bits in row
        if bit_idx > 0:
            row_bytes.append(current_byte)
            
        byte_array.extend(row_bytes)
        
    return byte_array

def write_c_file(data):
    hex_data = ", ".join([f"0x{b:02x}" for b in data])
    
    # Break into lines for readability
    lines = []
    chunk_size = 30 # bytes per line
    data_strings = [f"0x{b:02x}" for b in data]
    
    for i in range(0, len(data_strings), chunk_size):
        lines.append(", ".join(data_strings[i:i+chunk_size]) + ",")
        
    formatted_data = "\n  ".join(lines)
    
    content = f"""#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020
#define LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_AIPLAN_LOGO_FINAL_2020 uint8_t AIPLAN_LOGO_FINAL_2020_map[] = {{
  {formatted_data}
}};

const lv_img_dsc_t AIPLAN_LOGO_FINAL_2020 = {{
  .header.cf = LV_IMG_CF_ALPHA_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {WIDTH},
  .header.h = {HEIGHT},
  .data_size = {len(data)},
  .data = AIPLAN_LOGO_FINAL_2020_map,
}};
"""
    
    with open(FILE_PATH, "w") as f:
        f.write(content)
        
    print(f"Generated {FILE_PATH} with size {len(data)} bytes.")

if __name__ == "__main__":
    data = generate_logo_data()
    write_c_file(data)
