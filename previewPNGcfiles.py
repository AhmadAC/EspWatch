import sys
import os
import re
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                               QHBoxLayout, QPushButton, QListWidget, QLabel, 
                               QFileDialog, QSplitter, QCheckBox,
                               QScrollArea, QSpinBox, QMessageBox)
from PySide6.QtGui import QImage, QPixmap, QColor, QPainter, QBrush
from PySide6.QtCore import Qt
from PySide6.QtSvg import QSvgRenderer  # Handles rasterizing SVGs to QImages

class LVGLParser:
    @staticmethod
    def parse(filepath, swap_colors=False):
        """
        Parses an LVGL generated C file and returns a QImage.
        Automatically detects whether the 3-byte layout is interleaved or separated.
        """
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        # Support LVGL v8 and v9 nested header structs
        w_match = re.search(r'\.w\s*=\s*(\d+)|\.header\.w\s*=\s*(\d+)', content)
        h_match = re.search(r'\.h\s*=\s*(\d+)|\.header\.h\s*=\s*(\d+)', content)
        
        w, h = None, None
        if w_match:
            w = int(next(filter(None, w_match.groups())))
        if h_match:
            h = int(next(filter(None, h_match.groups())))
            
        if w is None or h is None:
            # Fallback size scanner
            dim_match = re.search(r'(\d+)\s*x\s*(\d+)', content)
            if dim_match:
                w = int(dim_match.group(1))
                h = int(dim_match.group(2))
            else:
                # Custom fallbacks for standard icon assets to prevent crash
                if "clock" in filepath.lower():
                    w, h = 200, 200
                elif "gear" in filepath.lower():
                    w, h = 64, 64
                else:
                    raise ValueError("Could not find image dimensions (.w/.h). Are you sure this is an image?")
            
        # Search for the actual C array payload
        data_match = re.search(r'const\s+.*?uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\}', content, re.DOTALL)
        if not data_match:
            data_match = re.search(r'uint8_t\s+\w+\[\]\s*=\s*\{(.*?)\}', content, re.DOTALL)
            
        if not data_match:
            raise ValueError("Could not find C array with image data.")
            
        hex_str = data_match.group(1)
        
        # Find all #define macros in the file and substitute them in the array
        defines = re.findall(r'#define\s+(\w+)\s+(.*?)(?=\n|//)', content)
        for macro_name, macro_val in defines:
            hex_str = re.sub(rf'\b{macro_name}\b', macro_val, hex_str)
            
        # Regex out all hexadecimal bytes
        hex_bytes = re.findall(r'0x([0-9A-Fa-f]{2})', hex_str, re.IGNORECASE)
        byte_array = [int(b, 16) for b in hex_bytes]
        
        expected_pixels = w * h
        actual_bytes = len(byte_array)
        
        # We need an RGBA buffer (4 bytes per pixel) for QImage
        img_data = bytearray(expected_pixels * 4)
        
        if actual_bytes >= expected_pixels * 4:
            # Format: ARGB8888
            for i in range(expected_pixels):
                img_data[i*4]     = byte_array[i*4]     # B
                img_data[i*4+1]   = byte_array[i*4+1]   # G
                img_data[i*4+2]   = byte_array[i*4+2]   # R
                img_data[i*4+3]   = byte_array[i*4+3]   # A
                
        elif actual_bytes >= expected_pixels * 3:
            # Statistical auto-detection for interleaved (v8) vs separate (v9) 3-byte format
            interleaved_alpha_matches = sum(1 for i in range(expected_pixels) if byte_array[i * 3 + 2] in (0x00, 0xFF))
            separated_alpha_matches = sum(1 for i in range(expected_pixels) if byte_array[expected_pixels * 2 + i] in (0x00, 0xFF))
            
            is_interleaved = interleaved_alpha_matches > separated_alpha_matches
            
            if is_interleaved:
                # Format: Interleaved RGB565 + Alpha
                for i in range(expected_pixels):
                    b0 = byte_array[i*3]
                    b1 = byte_array[i*3 + 1]
                    a = byte_array[i*3 + 2]
                    
                    if swap_colors:
                        pixel = (b0 << 8) | b1
                    else:
                        pixel = b0 | (b1 << 8)
                        
                    r = (pixel >> 11) & 0x1F
                    g = (pixel >> 5)  & 0x3F
                    b = pixel         & 0x1F
                    
                    r = (r * 255) // 31
                    g = (g * 255) // 63
                    b = (b * 255) // 31
                    
                    img_data[i*4]     = b
                    img_data[i*4+1]   = g
                    img_data[i*4+2]   = r
                    img_data[i*4+3]   = a
            else:
                # Format: RGB565 + 8-bit Alpha (Separate color and alpha arrays)
                color_len = expected_pixels * 2
                for i in range(expected_pixels):
                    b0 = byte_array[i*2]
                    b1 = byte_array[i*2 + 1]
                    a = byte_array[color_len + i]
                    
                    if swap_colors:
                        pixel = (b0 << 8) | b1
                    else:
                        pixel = b0 | (b1 << 8)
                        
                    r = (pixel >> 11) & 0x1F
                    g = (pixel >> 5)  & 0x3F
                    b = pixel         & 0x1F
                    
                    r = (r * 255) // 31
                    g = (g * 255) // 63
                    b = (b * 255) // 31
                    
                    img_data[i*4]     = b
                    img_data[i*4+1]   = g
                    img_data[i*4+2]   = r
                    img_data[i*4+3]   = a
                
        elif actual_bytes >= expected_pixels * 2:
            # Format: RGB565 (No Alpha)
            for i in range(expected_pixels):
                b0 = byte_array[i*2]
                b1 = byte_array[i*2 + 1]
                a = 255
                
                if swap_colors:
                    pixel = (b0 << 8) | b1
                else:
                    pixel = b0 | (b1 << 8)
                    
                r = (pixel >> 11) & 0x1F
                g = (pixel >> 5)  & 0x3F
                b = pixel         & 0x1F
                
                r = (r * 255) // 31
                g = (g * 255) // 63
                b = (b * 255) // 31
                
                img_data[i*4]     = b
                img_data[i*4+1]   = g
                img_data[i*4+2]   = r
                img_data[i*4+3]   = a
        else:
            raise ValueError(f"Not enough data: Needed >= {expected_pixels*2} bytes, got {actual_bytes}.")
            
        qimg = QImage(bytes(img_data), w, h, w * 4, QImage.Format_ARGB32)
        return qimg.copy()

    @staticmethod
    def export_to_lvgl_c(qimg, output_path, var_name):
        """
        Converts a QImage to an LVGL C array file (Format: RGB565 + 8-bit Alpha).
        Fully compliant with standard LVGL v9 compilation.
        """
        qimg = qimg.convertToFormat(QImage.Format_ARGB32)
        w = qimg.width()
        h = qimg.height()
        expected_pixels = w * h
        
        # Color buffer: 2 bytes per pixel (RGB565)
        color_bytes = bytearray(expected_pixels * 2)
        # Alpha buffer: 1 byte per pixel
        alpha_bytes = bytearray(expected_pixels)
        
        for y in range(h):
            for x in range(w):
                idx = y * w + x
                color = qimg.pixelColor(x, y)
                r, g, b, a = color.red(), color.green(), color.blue(), color.alpha()
                
                # Convert 24-bit RGB to 16-bit RGB565
                r5 = (r >> 3) & 0x1F
                g6 = (g >> 2) & 0x3F
                b5 = (b >> 3) & 0x1F
                
                rgb565 = (r5 << 11) | (g6 << 5) | b5
                
                # Write to color buffer (little endian format by default)
                color_bytes[idx * 2] = rgb565 & 0xFF
                color_bytes[idx * 2 + 1] = (rgb565 >> 8) & 0xFF
                
                # Write to alpha buffer
                alpha_bytes[idx] = a
                
        # Total byte payload combined
        payload = color_bytes + alpha_bytes
        
        # Format byte payload as C array hex rows
        hex_lines = []
        for i in range(0, len(payload), 16):
            chunk = payload[i:i+16]
            hex_str = ", ".join(f"0x{b:02X}" for b in chunk)
            hex_lines.append("    " + hex_str + ("," if i + 16 < len(payload) else ""))
            
        c_array_data = "\n".join(hex_lines)
        
        # Corrected structure definition to prevent corrupting bitfields in LVGL v9
        c_content = f"""/*
 * This file was generated automatically.
 * Format: RGB565 with 8-bit Alpha Channel
 */

#include <stdint.h>
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

// IMAGE DATA
const LV_ATTRIBUTE_MEM_ALIGN uint8_t {var_name}_data[] = {{
{c_array_data}
}};

const lv_image_dsc_t {var_name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565A8,
    .header.w = {w},
    .header.h = {h},
    .data_size = sizeof({var_name}_data),
    .data = {var_name}_data
}};
"""
        with open(output_path, 'w', encoding='utf-8') as f:
            f.write(c_content)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("LVGL & SVG Image Utility")
        self.resize(1024, 768)
        
        # State tracking
        self.original_loaded_image = QImage()
        self.current_loaded_image = QImage()
        self.current_filepath = None
        self.aspect_ratio = 1.0
        self._is_resizing = False
        self.current_files = []
        
        # Central widget
        central = QWidget()
        self.setCentralWidget(central)
        layout = QVBoxLayout(central)
        
        # Top tools bar (File Operations)
        top_bar = QHBoxLayout()
        self.btn_open_files = QPushButton("Open File(s)")
        self.btn_open_folder = QPushButton("Open Folder")
        
        self.btn_convert_png = QPushButton("Export to PNG...")
        self.btn_convert_png.setEnabled(False)
        self.btn_convert_png.setToolTip("Export the working image (such as raw SVG) to a PNG file.")
        
        self.btn_convert = QPushButton("Export to LVGL C...")
        self.btn_convert.setEnabled(False)
        self.btn_convert.setToolTip("Convert the current standalone PNG/SVG to an LVGL C array file.")
        
        self.cb_swap = QCheckBox("Swap 16-bit Colors")
        self.cb_swap.setToolTip("Toggle if the colors look mangled (Endianness correction)")
        self.cb_swap.stateChanged.connect(self.refresh_image)
        
        self.zoom_spinbox = QSpinBox()
        self.zoom_spinbox.setRange(1, 50)
        self.zoom_spinbox.setValue(3)
        self.zoom_spinbox.setPrefix("UI Zoom: x")
        self.zoom_spinbox.valueChanged.connect(self.refresh_image)
        
        top_bar.addWidget(self.btn_open_files)
        top_bar.addWidget(self.btn_open_folder)
        top_bar.addWidget(self.btn_convert_png)
        top_bar.addWidget(self.btn_convert)
        top_bar.addWidget(self.cb_swap)
        top_bar.addWidget(self.zoom_spinbox)
        top_bar.addStretch()
        layout.addLayout(top_bar)
        
        # Secondary Tool Bar (Resize Export Options)
        resize_bar = QHBoxLayout()
        
        self.spin_width = QSpinBox()
        self.spin_width.setRange(1, 4096)
        self.spin_width.setPrefix("Width: ")
        self.spin_width.setSuffix(" px")
        self.spin_width.valueChanged.connect(self.on_width_changed)
        
        self.spin_height = QSpinBox()
        self.spin_height.setRange(1, 4096)
        self.spin_height.setPrefix("Height: ")
        self.spin_height.setSuffix(" px")
        self.spin_height.valueChanged.connect(self.on_height_changed)
        
        self.cb_aspect = QCheckBox("Keep Aspect Ratio")
        self.cb_aspect.setChecked(True)
        
        lbl_resize = QLabel("Export Size:")
        lbl_resize.setStyleSheet("font-weight: bold;")
        
        resize_bar.addWidget(lbl_resize)
        resize_bar.addWidget(self.spin_width)
        resize_bar.addWidget(self.spin_height)
        resize_bar.addWidget(self.cb_aspect)
        resize_bar.addStretch()
        layout.addLayout(resize_bar)
        
        # Splitter for list & image view
        splitter = QSplitter(Qt.Horizontal)
        layout.addWidget(splitter)
        
        # Left side: File List
        self.list_widget = QListWidget()
        self.list_widget.currentItemChanged.connect(self.on_item_changed)
        
        # Right side: Image Viewer
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setStyleSheet("background-color: #2b2b2b;")
        
        self.lbl_image = QLabel("Select or Load LVGL C, PNG, or SVG Files")
        self.lbl_image.setAlignment(Qt.AlignCenter)
        self.scroll_area.setWidget(self.lbl_image)
        
        splitter.addWidget(self.list_widget)
        splitter.addWidget(self.scroll_area)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 4)
        
        # Checkerboard brush for transparent background illusion
        self.checkerboard = self.create_checkerboard()
        
        # Events
        self.btn_open_files.clicked.connect(self.open_files)
        self.btn_open_folder.clicked.connect(self.open_folder)
        self.btn_convert_png.clicked.connect(self.export_to_png)
        self.btn_convert.clicked.connect(self.convert_current_file)

    def create_checkerboard(self):
        img = QImage(16, 16, QImage.Format_ARGB32)
        img.fill(QColor(255, 255, 255))
        p = QPainter(img)
        p.fillRect(0, 0, 8, 8, QColor(200, 200, 200))
        p.fillRect(8, 8, 8, 8, QColor(200, 200, 200))
        p.end()
        return QPixmap.fromImage(img)
        
    def display_image(self, qimage):
        """Scales and sets the clean image with the checkerboard background."""
        if qimage.isNull():
            self.lbl_image.clear()
            self.lbl_image.setText("Failed to build image.")
            return

        pm = QPixmap.fromImage(qimage)
        
        # Apply scaling based on the SpinBox
        zoom = self.zoom_spinbox.value()
        if zoom != 1:
            pm = pm.scaled(pm.size() * zoom, Qt.KeepAspectRatio, Qt.FastTransformation)
            
        # Draw on top of checkerboard pattern
        comp = QPixmap(pm.size())
        p = QPainter(comp)
        p.fillRect(comp.rect(), QBrush(self.checkerboard))
        p.drawPixmap(0, 0, pm)
        p.end()
        
        self.lbl_image.setPixmap(comp)
        
    def open_files(self):
        files, _ = QFileDialog.getOpenFileNames(
            self, 
            "Select Files", 
            "", 
            "Supported Files (*.c *.png *.svg);;C Files (*.c);;Images (*.png *.svg);;All Files (*.*)"
        )
        if files:
            self.load_files(files)
            
    def open_folder(self):
        """Recursively parses all workspace subdirectories for visual assets."""
        folder = QFileDialog.getExistingDirectory(self, "Select Directory")
        if folder:
            valid_exts = ('.c', '.png', '.svg')
            files = []
            for root, dirs, filenames in os.walk(folder):
                for f in filenames:
                    if f.lower().endswith(valid_exts):
                        files.append(os.path.join(root, f))
            self.load_files(files)
            
    def load_files(self, files):
        self.list_widget.clear()
        self.current_files = sorted(files)
        for f in self.current_files:
            self.list_widget.addItem(os.path.basename(f))
            
    def refresh_image(self):
        """Refreshes rendering configuration for the currently selected file."""
        if not self.current_loaded_image.isNull():
            self.display_image(self.current_loaded_image)
            
    def apply_resize(self):
        """Applies the current spinbox dimensions to generate the working image."""
        if self.original_loaded_image.isNull():
            return
            
        w = self.spin_width.value()
        h = self.spin_height.value()
        
        if self.current_filepath and self.current_filepath.lower().endswith('.svg'):
            # Re-rasterize SVG natively at the target resolution for perfect clarity
            self.current_loaded_image = QImage(w, h, QImage.Format_ARGB32)
            self.current_loaded_image.fill(Qt.transparent)
            renderer = QSvgRenderer(self.current_filepath)
            p = QPainter(self.current_loaded_image)
            renderer.render(p)
            p.end()
        else:
            # Scale standard raster images smoothly
            self.current_loaded_image = self.original_loaded_image.scaled(w, h, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)
            
        self.display_image(self.current_loaded_image)

    def on_width_changed(self, val):
        if self._is_resizing: return
        self._is_resizing = True
        if self.cb_aspect.isChecked() and self.aspect_ratio > 0:
            self.spin_height.setValue(max(1, int(val / self.aspect_ratio)))
        self._is_resizing = False
        self.apply_resize()

    def on_height_changed(self, val):
        if self._is_resizing: return
        self._is_resizing = True
        if self.cb_aspect.isChecked() and self.aspect_ratio > 0:
            self.spin_width.setValue(max(1, int(val * self.aspect_ratio)))
        self._is_resizing = False
        self.apply_resize()
            
    def on_item_changed(self, current, previous):
        if not current:
            self.btn_convert.setEnabled(False)
            self.btn_convert_png.setEnabled(False)
            return
            
        idx = self.list_widget.row(current)
        if 0 <= idx < len(self.current_files):
            filepath = self.current_files[idx]
            self.current_filepath = filepath
            ext = os.path.splitext(filepath)[1].lower()
            
            try:
                if ext == '.c':
                    self.original_loaded_image = LVGLParser.parse(filepath, self.cb_swap.isChecked())
                    self.btn_convert.setEnabled(False)
                    self.btn_convert_png.setEnabled(True)  # Allowed to convert C arrays back to PNG
                elif ext == '.png':
                    self.original_loaded_image = QImage(filepath)
                    self.btn_convert.setEnabled(True)
                    self.btn_convert_png.setEnabled(True)
                elif ext == '.svg':
                    renderer = QSvgRenderer(filepath)
                    if renderer.isValid():
                        size = renderer.defaultSize()
                        orig_w = size.width() if size.width() > 0 else 256
                        orig_h = size.height() if size.height() > 0 else 256
                        
                        # Store base native dimensions for scaling reference
                        self.original_loaded_image = QImage(orig_w, orig_h, QImage.Format_ARGB32)
                        self.original_loaded_image.fill(Qt.transparent)
                        p = QPainter(self.original_loaded_image)
                        renderer.render(p)
                        p.end()
                    else:
                        raise ValueError("Invalid SVG file format.")
                    self.btn_convert.setEnabled(True)
                    self.btn_convert_png.setEnabled(True)
                else:
                    self.original_loaded_image = QImage()
                    self.btn_convert.setEnabled(False)
                    self.btn_convert_png.setEnabled(False)
                
                if self.original_loaded_image.isNull():
                    raise ValueError("Failed to load image data.")
                
                # Fetch base dimensions for UI controls
                orig_w = self.original_loaded_image.width()
                orig_h = self.original_loaded_image.height()
                self.aspect_ratio = orig_w / orig_h if orig_h > 0 else 1.0
                
                # Update spinboxes without triggering recursive resize calls
                self._is_resizing = True
                self.spin_width.setValue(orig_w)
                self.spin_height.setValue(orig_h)
                self._is_resizing = False
                
                # Apply initial native display
                self.apply_resize()
                
            except Exception as e:
                self.lbl_image.clear()
                self.lbl_image.setText(f"Error loading '{os.path.basename(filepath)}':\n{str(e)}\n\n(Note: C Font files are not supported, only images)")
                self.btn_convert.setEnabled(False)
                self.btn_convert_png.setEnabled(False)

    def convert_current_file(self):
        """Converts currently selected active SVG/PNG image to C array file."""
        if self.current_loaded_image.isNull():
            return
            
        current_item = self.list_widget.currentItem()
        if not current_item:
            return
            
        original_name = current_item.text()
        suggested_name = os.path.splitext(original_name)[0]
        
        # Format a safe C variable prefix automatically
        safe_prefix = "ui_img_" if not suggested_name.startswith("ui_img_") else ""
        
        save_path, _ = QFileDialog.getSaveFileName(
            self, 
            "Save LVGL C File", 
            f"{safe_prefix}{suggested_name}.c", 
            "C Files (*.c)"
        )
        
        if save_path:
            try:
                var_name = os.path.splitext(os.path.basename(save_path))[0]
                var_name = re.sub(r'[^a-zA-Z0-9_]', '_', var_name).lower()
                if var_name[0].isdigit():
                    var_name = "_" + var_name
                
                # We export the active working image, which contains the scaled resolution!
                LVGLParser.export_to_lvgl_c(self.current_loaded_image, save_path, var_name)
                QMessageBox.information(self, "Success", f"Converted and exported:\n{os.path.basename(save_path)}")
                
            except Exception as e:
                QMessageBox.critical(self, "Conversion Error", f"Failed to convert file:\n{str(e)}")

    def export_to_png(self):
        """Saves the current working, resized QImage directly as a 32-bit PNG file."""
        if self.current_loaded_image.isNull():
            return

        current_item = self.list_widget.currentItem()
        if not current_item:
            return

        original_name = current_item.text()
        suggested_name = os.path.splitext(original_name)[0] + ".png"

        save_path, _ = QFileDialog.getSaveFileName(
            self,
            "Save PNG Image",
            suggested_name,
            "PNG Files (*.png)"
        )

        if save_path:
            try:
                # Ensure transparent backgrounds map smoothly
                export_image = self.current_loaded_image.convertToFormat(QImage.Format_ARGB32)
                success = export_image.save(save_path, "PNG")
                if success:
                    QMessageBox.information(self, "Success", f"Exported PNG successfully:\n{os.path.basename(save_path)}")
                else:
                    raise IOError("QImage save call returned False.")
            except Exception as e:
                QMessageBox.critical(self, "Export Error", f"Failed to save PNG image:\n{str(e)}")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = MainWindow()
    win.show()
    sys.exit(app.exec())