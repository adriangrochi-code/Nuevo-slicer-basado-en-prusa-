"""Evaluador de las condiciones de compatibilidad de PrusaSlicer 2.x.

Cubre la gramática que usan los paquetes de fabricante:
``printer_notes=~/.*PRINTER_VENDOR_CREALITY.*/ and nozzle_diameter[0]==0.4``,
con ``and``/``or``/``not``/``!``/``&&``/``||``, paréntesis, ``== != < <= > >=``,
``=~ !~`` (regex), cadenas, números, booleanos y variables con índice ``[i]``.
Las variables salen de la configuración aplanada del preset de impresora (o de
impresión, para ``compatible_prints_condition``).
"""

from __future__ import annotations

import re
from typing import Any

from .ini29 import parse_strings, unescape

_TOKEN = re.compile(r"""
    \s*(?:
      (?P<num>\d+(?:\.\d*)?|\.\d+)
    | (?P<str>"(?:\\.|[^"\\])*")
    | (?P<re>/(?:\\.|[^/\\])*/)
    | (?P<op>==|!=|<=|>=|=~|!~|&&|\|\||[<>!()\[\]])
    | (?P<id>[A-Za-z_][A-Za-z_0-9]*)
    )""", re.X)


class CondError(ValueError):
    pass


def tokenize(expr: str) -> list[tuple[str, str]]:
    toks, pos = [], 0
    expr = expr.strip()
    while pos < len(expr):
        m = _TOKEN.match(expr, pos)
        if not m or m.end() == pos:
            raise CondError(f"no se entiende '{expr[pos:pos + 20]}' en: {expr}")
        kind = m.lastgroup
        text = m.group(kind)
        if kind == "id" and text in ("and", "or", "not", "true", "false"):
            kind = text if text in ("and", "or", "not") else "bool"
        toks.append((kind, text))
        pos = m.end()
    return toks


class Evaluator:
    def __init__(self, variables: dict[str, str]):
        self.vars = variables

    def __call__(self, expr: str) -> bool:
        if not expr.strip():
            return True
        self.toks = tokenize(expr)
        self.i = 0
        value = self._or()
        if self.i != len(self.toks):
            raise CondError(f"sobra '{self.toks[self.i][1]}' en: {expr}")
        return bool(value)

    # -- gramática ------------------------------------------------------
    def _peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def _take(self, *texts):
        kind, text = self._peek()
        if text in texts or kind in texts:
            self.i += 1
            return text
        return None

    def _or(self):
        v = self._and()
        while self._take("or", "||"):
            r = self._and()
            v = bool(v) or bool(r)
        return v

    def _and(self):
        v = self._not()
        while self._take("and", "&&"):
            r = self._not()
            v = bool(v) and bool(r)
        return v

    def _not(self):
        if self._take("not", "!"):
            return not self._not()
        return self._cmp()

    def _cmp(self):
        left = self._atom()
        op = self._take("==", "!=", "<", "<=", ">", ">=", "=~", "!~")
        if not op:
            return _truthy(left)
        if op in ("=~", "!~"):
            kind, text = self._peek()
            if kind != "re":
                raise CondError("se esperaba /regex/ tras " + op)
            self.i += 1
            hit = re.fullmatch(text[1:-1], str(left), re.S) is not None
            return hit if op == "=~" else not hit
        right = self._atom()
        a, b = _coerce(left, right)
        return {"==": a == b, "!=": a != b, "<": a < b, "<=": a <= b,
                ">": a > b, ">=": a >= b}[op]

    def _atom(self) -> Any:
        kind, text = self._peek()
        if kind is None:
            raise CondError("expresión incompleta")
        self.i += 1
        if text == "(":
            v = self._or()
            if not self._take(")"):
                raise CondError("falta ')'")
            return v
        if kind == "num":
            return float(text)
        if kind == "str":
            return parse_strings(text)[0] if text != '""' else ""
        if kind == "bool":
            return text == "true"
        if kind == "id":
            return self._var(text)
        raise CondError(f"token inesperado '{text}'")

    def _var(self, name: str):
        index = None
        if self._take("["):
            k, t = self._peek()
            self.i += 1
            index = int(float(t))
            if not self._take("]"):
                raise CondError("falta ']'")
        raw = self.vars.get(name)
        if raw is None:
            return ""
        if raw.startswith('"'):
            items = parse_strings(raw)
        else:
            items = [p.strip() for p in raw.split(",")] if index is not None else [raw]
        if index is not None:
            if index >= len(items):
                return ""
            raw = items[index]
        elif raw.startswith('"'):
            raw = items[0] if items else ""
        else:
            raw = unescape(raw)
        return _value(raw)


def _value(s: str):
    try:
        return float(s)
    except ValueError:
        return s


def _truthy(v) -> bool:
    if isinstance(v, str):
        return v not in ("", "0", "false")
    return bool(v)


def _coerce(a, b):
    if isinstance(a, float) and isinstance(b, str):
        try:
            return a, float(b)
        except ValueError:
            return str(a), b
    if isinstance(b, float) and isinstance(a, str):
        try:
            return float(a), b
        except ValueError:
            return a, str(b)
    if isinstance(a, bool) or isinstance(b, bool):
        return _truthy(a), _truthy(b)
    return a, b
