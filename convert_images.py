#!/usr/bin/env python3
import os
import sys
import re

def optimize_png(filename):
    """Optimise un PNG en réduisant sa taille"""
    try:
        from PIL import Image
        import io
        
        with Image.open(filename) as img:
            # Redimensionner à 24x24 pour économiser de l'espace
            # (sauf tuxmgr 100x100 et icônes fenêtre qxtask/qxtask_root 64x64)
            basename = os.path.basename(filename)
            keep_size = basename in ("tuxmgr.png", "qxtask.png", "qxtask_root.png", "root.png")
            if not keep_size and img.size != (24, 24):
                img = img.resize((24, 24), Image.LANCZOS)
            
            # Convertir en mode palette si possible (réduit drastiquement la taille)
            if img.mode in ('RGBA', 'RGB'):
                # Réduire le nombre de couleurs pour les icônes système
                img = img.convert('P', palette=Image.ADAPTIVE, colors=16)
            
            # Sauvegarder avec compression maximale
            buffer = io.BytesIO()
            img.save(buffer, format='PNG', optimize=True, compress_level=9)
            return buffer.getvalue()
    except ImportError:
        print("PIL/Pillow non installé, utilisation du fichier original")
        with open(filename, 'rb') as f:
            return f.read()
    except Exception as e:
        print(f"Erreur lors de l'optimisation de {filename}: {e}")
        with open(filename, 'rb') as f:
            return f.read()

def process_file(filename):
    basename = os.path.basename(filename)
    varname = re.sub(r'[^a-zA-Z0-9_]', '_', os.path.splitext(basename)[0])
    
    # Optimiser l'image
    data = optimize_png(filename)
    original_size = os.path.getsize(filename)
    
    output = f"// Optimized data for {basename} (original: {original_size} bytes, optimized: {len(data)} bytes)\n"
    output += f"static const unsigned char {varname}_data[] = {{\n    "

    byte_count = 0
    for byte in data:
        output += f"0x{byte:02x}, "
        byte_count += 1
        if byte_count % 12 == 0:
            output += "\n    "
    
    # Supprimer la dernière virgule et espace si nécessaire
    if output.endswith(", "):
        output = output[:-2]
    
    output += "\n};\n"
    output += f"static const size_t {varname}_size = sizeof({varname}_data);\n\n"
    
    return output

def generate_header_file(directory):
    pattern = re.compile(r'.*\.png')
    image_files = [os.path.join(directory, f) for f in os.listdir(directory)
                   if pattern.match(f) or f in ("qxtask.png")]
    
    if not image_files:
        print(f"Aucune icône trouvée dans {directory}")
        return None

    # Trier les fichiers pour un ordre cohérent
    image_files.sort()

    header_content = "/* Header file auto-generated for optimized icons */\n"
    header_content += "#ifndef TASKMGR_ICONS_H\n"
    header_content += "#define TASKMGR_ICONS_H\n\n"
    header_content += "#include <stddef.h>\n\n"

    total_original = 0
    total_optimized = 0
    
    for file in image_files:
        file_content = process_file(file)
        header_content += file_content
        
        # Calculer les tailles pour les statistiques
        original_size = os.path.getsize(file)
        total_original += original_size
    
    header_content += f"/* Total: {len(image_files)} icons */\n"
    header_content += f"/* Original total size: {total_original} bytes */\n"
    header_content += "#endif /* TASKMGR_ICONS_H */\n"
    
    with open("taskmgr_icons.h", "w") as f:
        f.write(header_content)
    
    print(f"taskmgr_icons.h généré: {len(image_files)} icônes")
    print(f"Taille originale totale: {total_original} bytes")
    return "taskmgr_icons.h"

def generate_minimal_icons(directory):
    """Génère seulement les icônes essentielles pour réduire la taille"""
    essential_icons = ["cpu0.png", "cpu5.png", "cpu10.png", "qxtask.png"]  # Seulement 4 niveaux
    
    image_files = []
    for icon in essential_icons:
        full_path = os.path.join(directory, icon)
        if os.path.exists(full_path):
            image_files.append(full_path)
        else:
            print(f"Attention: {icon} non trouvée")
    
    if not image_files:
        print(f"Aucune icône essentielle trouvée dans {directory}")
        return None

    header_content = "/* Header file auto-generated for essential icons only */\n"
    header_content += "#ifndef TASKMGR_ICONS_H\n"
    header_content += "#define TASKMGR_ICONS_H\n\n"
    header_content += "#include <stddef.h>\n\n"
    
    for file in image_files:
        header_content += process_file(file)
    
    header_content += f"/* Minimal set: {len(image_files)} icons */\n"
    header_content += "#endif /* TASKMGR_ICONS_H */\n"
    
    with open("taskmgr_icons.h", "w") as f:
        f.write(header_content)
    
    print(f"Version minimale générée: {len(image_files)} icônes essentielles")
    return "taskmgr_icons.h"

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 convert_images.py /chemin/vers/les/icones/ [--minimal]")
        sys.exit(1)
    
    icon_dir = sys.argv[1]
    if not os.path.isdir(icon_dir):
        print(f"Le dossier {icon_dir} n'existe pas")
        sys.exit(1)
    
    minimal = "--minimal" in sys.argv
    
    if minimal:
        generate_minimal_icons(icon_dir)
    else:
        generate_header_file(icon_dir)
	