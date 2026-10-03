"""NPS: slicer no planar construido sobre el motor de PrusaSlicer.

Flujo: malla -> deformación (espacio de corte) -> PrusaSlicer (corte planar)
-> transformación inversa del G-code (espacio real, capas curvas).
"""

__version__ = "0.1.0"
