from pathlib import Path
from odf.opendocument import load
from odf.table import Table, TableRow, TableCell
from odf.text import P

ods_path = Path("mechanics.ods")
output_ods_path = Path("output.ods")

def remove_sheet_by_name(doc, sheet_name):
    for table in doc.spreadsheet.getElementsByType(Table):
        if table.getAttribute("name") == sheet_name:
            doc.spreadsheet.removeChild(table)
            break

def cell_text(cell):
    parts = []
    for p in cell.getElementsByType(P):
        if p.firstChild:
            parts.append(str(p.firstChild.data))
    return "".join(parts).strip()

def col_index_to_letter(idx):
    """Convert zero-based index to spreadsheet column letters (0 -> A, 25 -> Z, 26 -> AA)."""
    letters = ""
    while idx >= 0:
        letters = chr(ord('A') + (idx % 26)) + letters
        idx = idx // 26 - 1
    return letters

# Load input document
doc = load(str(ods_path))

Mechanics_Sheet = doc.spreadsheet.getElementsByType(Table)[0]
Fate_Sheet = doc.spreadsheet.getElementsByType(Table)[1]

# Build mechanics list
mechanics = []
for row in Mechanics_Sheet.getElementsByType(TableRow)[1:]:
    cells = row.getElementsByType(TableCell)
    if not cells:
        continue
    name = cell_text(cells[0])
    if name:
        mechanics.append(name)

# Initialize pair matrix
header_row = [""] + mechanics
pair_matrix = [header_row]
for m in mechanics:
    pair_matrix.append([m] + [[] for _ in mechanics])

# Parse Fate_Sheet into game item blocks
rows = Fate_Sheet.getElementsByType(TableRow)

class Game_Item:
    def __init__(self, name, mechanics):
        self.name = name
        self.mechanics = mechanics

in_game_item = False
game_item = None

for row in rows:
    row_data = []
    for cell in row.getElementsByType(TableCell):
        if cell.getAttribute("numbercolumnsrepeated") and int(cell.getAttribute("numbercolumnsrepeated")) == 2:
            row_data.append("")
        row_data.append(cell_text(cell) if cell.getElementsByType(P) else "")
    # skip act marker rows
    if len(row_data) > 1 and row_data[1] in ("A", "B", "C"):
        continue

    if not in_game_item:
        if len(row_data) > 1 and row_data[1]:
            in_game_item = True
            game_item = Game_Item(row_data[1], set())
            if len(row_data) > 2 and row_data[2]:
                game_item.mechanics.add(row_data[2])
    else:
        if len(row_data) > 2 and row_data[2]:
            game_item.mechanics.add(row_data[2])
        else:
            for mech_a in game_item.mechanics:
                for mech_b in game_item.mechanics:
                    if mech_a != mech_b and mech_a in mechanics and mech_b in mechanics:
                        ai = mechanics.index(mech_a) + 1
                        bi = mechanics.index(mech_b) + 1
                        pair_matrix[ai][bi].append(game_item)
            in_game_item = False

# Create Game_Item_Output sheet and store pair label cell positions
remove_sheet_by_name(doc, "Game_Item_Output")
game_item_table = Table(name="Game_Item_Output")

pair_positions = {}  # map "A x B" -> cell address string e.g. "A5"

current_row_index = 0
for i in range(1, len(pair_matrix)):
    for j in range(1, len(pair_matrix[i])):
        if i != j:
            mech_i = mechanics[i - 1]
            mech_j = mechanics[j - 1]
            items = pair_matrix[i][j]

            pair_text = f"{mech_i} x {mech_j}"

            tr = TableRow()
            tc_pair = TableCell()
            tc_pair.addElement(P(text=pair_text))
            tr.addElement(tc_pair)

            if items:
                first_item_cell = TableCell()
                first_item_cell.addElement(P(text=items[0].name))
                tr.addElement(first_item_cell)

            game_item_table.addElement(tr)

            # Save the cell address of pair label: columns are 0-based, row also 0-based
            # The pair label is always in column 0 of this row
            col_letter = col_index_to_letter(0)  # always 'A'
            row_number = current_row_index + 1  # Calc rows start at 1
            cell_address = f"{col_letter}{row_number}"
            pair_positions[pair_text] = cell_address

            current_row_index += 1

            # Add following rows for extra items
            for game_item in items[1:]:
                tr = TableRow()
                tr.addElement(TableCell())  # empty pair cell
                tc_item = TableCell()
                tc_item.addElement(P(text=game_item.name))
                tr.addElement(tc_item)
                game_item_table.addElement(tr)
                current_row_index += 1

doc.spreadsheet.addElement(game_item_table)

# Create above, but just counts

remove_sheet_by_name(doc, "Game_Item_Count_Output")
game_item_count_table = Table(name="Game_Item_Count_Output")

for i in range(1, len(pair_matrix)):
    for j in range(1 + 1, len(pair_matrix[i])):
        if i != j:
            mech_i = mechanics[i - 1]
            mech_j = mechanics[j - 1]
            items = pair_matrix[i][j]

            pair_text = f"{mech_i} x {mech_j}"

            tr = TableRow()
            tc_pair = TableCell()
            tc_pair.addElement(P(text=pair_text))
            tr.addElement(tc_pair)

            first_item_cell = TableCell(valuetype="float", value=len(items))
            tr.addElement(first_item_cell)

            game_item_count_table.addElement(tr)

doc.spreadsheet.addElement(game_item_count_table)

# Create Mechanic_Pair_Output sheet with hyperlinks pointing to Game_Item_Output cells
remove_sheet_by_name(doc, "Mechanic_Pair_Output")

header_row = [""] + mechanics
pair_matrix_with_links = [header_row]

for i, mech_i in enumerate(mechanics):
    row = [mech_i]
    for j, mech_j in enumerate(mechanics):
        if i == j:
            row.append("")
        else:
            pair_text = f"{mech_i} x {mech_j}"
            target_cell = pair_positions.get(pair_text)
            if target_cell:
                link = f'#Game_Item_Output.{target_cell}'
                formula = f'=HYPERLINK("{link}"; {len(pair_matrix[i + 1][j + 1])})'
                row.append({"formula": formula, "text": "View"})
            else:
                row.append("")
    pair_matrix_with_links.append(row)

def create_sheet_with_links(sheet_name, data):
    table = Table(name=sheet_name)
    for row_data in data:
        tr = TableRow()
        for cell_data in row_data:
            if isinstance(cell_data, dict) and "formula" in cell_data:
                tc = TableCell(valuetype="float", formula=cell_data["formula"])
                # tc.addElement(P(text=cell_data["text"]))
                tr.addElement(tc)
            elif isinstance(cell_data, str):
                tc = TableCell()
                tc.addElement(P(text=cell_data))
                tr.addElement(tc)
            else:
                tc = TableCell()
                tc.addElement(P(text=str(cell_data)))
                tr.addElement(tc)
        table.addElement(tr)
    return table

doc.spreadsheet.addElement(create_sheet_with_links("Mechanic_Pair_Output", pair_matrix_with_links))

# Save result
doc.save(str(output_ods_path))
