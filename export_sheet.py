from pathlib import Path
from odf.opendocument import load
from odf.table import Table, TableRow, TableCell
from odf.text import P

ods_path = Path("mechanics.ods")
output_ods_path = Path("output.ods")

def remove_sheet_by_name(doc, sheet_name):
    """Remove a sheet (table) by its name if it exists."""
    for table in doc.spreadsheet.getElementsByType(Table):
        if table.getAttribute("name") == sheet_name:
            doc.spreadsheet.removeChild(table)
            break

def create_sheet(sheet_name, data):
    """Create a sheet from a list of lists (rows)."""
    table = Table(name=sheet_name)
    for row_data in data:
        tr = TableRow()
        for cell_data in row_data:
            tc = TableCell()
            tc.addElement(P(text=str(cell_data)))
            tr.addElement(tc)
        table.addElement(tr)
    return table

def cell_text(cell):
    """Extract visible text from a TableCell."""
    parts = []
    for p in cell.getElementsByType(P):
        if p.firstChild:
            parts.append(str(p.firstChild.data))
    return "".join(parts).strip()

# Load input document
doc = load(str(ods_path))

# Get source sheets
Mechanics_Sheet = doc.spreadsheet.getElementsByType(Table)[0]  # List of all mechanics
Fate_Sheet      = doc.spreadsheet.getElementsByType(Table)[1]  # Game items with mechanics

# --------------------------------------------------
# Build mechanics list from Mechanics_Sheet col 0
# --------------------------------------------------
mechanics = []
for row in Mechanics_Sheet.getElementsByType(TableRow)[1:]:
    cells = row.getElementsByType(TableCell)
    if not cells:
        continue
    name = cell_text(cells[0])
    if name:
        mechanics.append(name)

# --------------------------------------------------
# Initialize pair matrix with 0 counts
# --------------------------------------------------
header_row = [""] + mechanics
pair_matrix = [header_row]
for m in mechanics:
    pair_matrix.append([m] + [0] * len(mechanics))

# --------------------------------------------------
# Parse Fate_Sheet into game item blocks
# --------------------------------------------------
rows = Fate_Sheet.getElementsByType(TableRow)

in_game_item = False
mechanic_collection = set()

for row in rows:
    # Get text from all cells
    row_data = []
    for cell in row.getElementsByType(TableCell):
        row_data.append(cell_text(cell) if cell.getElementsByType(P) else "")

    # Skip act marker rows like "A", "B", or "C"
    if len(row_data) > 1 and row_data[1] in ("A", "B") or \
       len(row_data) > 2 and row_data[2] == "C":
        continue

    if not in_game_item:
        # Start of a new block
        if len(row_data) > 1 and row_data[1]:
            in_game_item = True
            if len(row_data) > 2 and row_data[2]:
                mechanic_collection.add(row_data[2])
    else:
        # Inside a block
        if len(row_data) > 2 and row_data[2]:
            mechanic_collection.add(row_data[2])
        else:
            # End of block — increment matrix for all pairs
            for mech_a in mechanic_collection:
                for mech_b in mechanic_collection:
                    if mech_a in mechanics and mech_b in mechanics:
                        ai = mechanics.index(mech_a) + 1
                        bi = mechanics.index(mech_b) + 1
                        pair_matrix[ai][bi] += 1

            mechanic_collection.clear()
            in_game_item = False

# --------------------------------------------------
# Replace sheets with new generated data
# --------------------------------------------------
remove_sheet_by_name(doc, "Mechanic_Pair_Output")
doc.spreadsheet.addElement(create_sheet("Mechanic_Pair_Output", pair_matrix))

# Save result
doc.save(str(output_ods_path))
