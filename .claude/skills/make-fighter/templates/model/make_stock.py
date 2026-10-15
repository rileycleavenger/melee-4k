"""<NAME>'s stock icon: pixel art drawn at the game's 24x24, like every
Melee stock icon (build_ui.py scales it up by nearest neighbour for the
HD copy). The head in three-quarter view, a dark outline, 2-3 shades per
material, colours sampled from the model's textures.

    python make_stock.py out.png [preview.png]
"""
import sys
from PIL import Image

PAL = {
    ".": (0, 0, 0, 0),           # transparent
    "K": (28, 18, 30, 255),      # outline
    "S": (250, 210, 170, 255),   # skin light (replace: the character's colours)
    "s": (210, 160, 120, 255),   # skin shade
    "H": (120, 70, 40, 255),     # hair
    "h": (80, 45, 25, 255),      # hair shade
    "E": (252, 250, 246, 255),   # eye white
    "P": (30, 30, 60, 255),      # pupil
}

ART = [  # 24 rows of 24: replace with the character's head
    "........................",
    "........KKKKKKKK........",
    "......KKHHHHHHHHKK......",
    ".....KHHHHHHHHHHHHK.....",
    "....KHHHhHHHHHhHHHHK....",
    "....KHHhhhHHHHhhHHHK....",
    "...KHHSSSSSSSSSSSHHHK...",
    "...KHSSSSSSSSSSSSSHHK...",
    "...KHSSEEPSSSEEPSSSHK...",
    "...KHSSEEPSSSEEPSSSHK...",
    "...KSSSSSSSSSSSSSSSSK...",
    "...KSSSSSSSsSSSSSSSsK...",
    "....KSSSSSSSSSSSSSSsK...",
    "....KSSSSKKKKKSSSSsK....",
    ".....KSSSSSSSSSSSssK....",
    "......KsSSSSSSSSssK.....",
    ".......KKsssssssKK......",
    ".........KKKKKKK........",
    "........................",
    "........................",
    "........................",
    "........................",
    "........................",
    "........................",
]


def draw():
    assert len(ART) == 24 and all(len(r) == 24 for r in ART), [len(r) for r in ART]
    img = Image.new("RGBA", (24, 24))
    img.putdata([PAL[c] for row in ART for c in row])
    return img


if __name__ == "__main__":
    img = draw()
    img.save(sys.argv[1])
    if len(sys.argv) > 2:
        bg = Image.new("RGBA", (24, 24), (40, 40, 60, 255))
        bg.alpha_composite(img)
        bg.resize((240, 240), Image.NEAREST).save(sys.argv[2])
