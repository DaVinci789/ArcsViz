from pathlib import Path
import yaml
from odf.opendocument import load
from odf.table import Table, TableRow, TableCell
from odf.text import P

# ====== Config ======
yaml_path = Path("blightedreach.yaml")
ods_path = Path("mechanics.ods")
output_ods_path = Path("data_with_ids.ods")
# ====================

# Load YAML
with open(yaml_path, "r", encoding="utf-8") as f:
    yaml_data = yaml.safe_load(f)

# Build lookup from name → list of IDs
name_to_ids = {}
for entry in yaml_data:
    name = entry.get("name")
    cid = entry.get("image")
    if name and cid:
        name_to_ids.setdefault(name.strip(), []).append(cid)

# Load ODS
doc = load(str(ods_path))
sheet = doc.spreadsheet.getElementsByType(Table)[1]  # first sheet

# Process each row
for row in sheet.getElementsByType(TableRow):
    cells = row.getElementsByType(TableCell)
    if len(cells) >= 2:  # ensure col B exists
        col_b_text = "".join(p.firstChild.data for p in cells[1].getElementsByType(P) if p.firstChild)  # Column B
        if col_b_text.strip():
            ids = name_to_ids.get(col_b_text.strip())
            if ids:
                # Ensure column D exists
                while len(cells) < 4:
                    new_cell = TableCell()
                    row.addElement(new_cell)
                    cells.append(new_cell)
                # Write IDs to col D
                # cells[3].clearContent()
                cells[3].addElement(P(text=", ".join(ids)))

# Save updated ODS
doc.save(str(output_ods_path))
print(f"Updated ODS saved to {output_ods_path}")
