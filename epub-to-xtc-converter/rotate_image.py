import sys
import os
from PIL import Image
import io

def rotate_image():
    try:
        if len(sys.argv) < 2:
            sys.stderr.write("Usage: python rotate_image.py <angle>\n")
            sys.exit(1)
            
        angle = int(sys.argv[1])
        if angle == 0:
            sys.stdout.buffer.write(sys.stdin.buffer.read())
            return

        # Read image from stdin
        img_data = sys.stdin.buffer.read()
        if not img_data:
            return
            
        img = Image.open(io.BytesIO(img_data))
        
        # PIL rotate is counter-clockwise. 
        # XTC converter rotation logic (90/270) appears to be matching 
        # clockwise behavior in its pixel mapping.
        # Let's use -angle to rotate clockwise.
        rotated = img.rotate(-angle, expand=True)
        
        # Save back to buffer
        output = io.BytesIO()
        # Preserve original format or use JPEG as fallback
        fmt = img.format if img.format else "JPEG"
        rotated.save(output, format=fmt, quality=95)
        
        sys.stdout.buffer.write(output.getvalue())
    except Exception as e:
        sys.stderr.write(str(e))
        sys.exit(1)

if __name__ == "__main__":
    rotate_image()
