#!/usr/bin/env bash
# Trae una versión nueva de PrusaSlicer al fork aplicando el diff entre etiquetas.
#
#   tools/update-upstream.sh version_3.0.0-alpha12 version_3.0.0-alpha13
#
# El fork se importó sin el historial de Prusa, así que en vez de un merge se
# aplica el cambio entre la etiqueta actual (FROM) y la nueva (TO) con 3-way
# (los conflictos quedan marcados en los ficheros). nps-prototype/ y los
# ficheros propios del fork no se tocan. Con --check sólo comprueba.
set -euo pipefail
FROM=${1:?etiqueta actual, p. ej. version_3.0.0-alpha12}
TO=${2:?etiqueta nueva, p. ej. version_3.0.0-alpha13}
MODE=${3:-}
UPSTREAM=https://github.com/prusa3d/PrusaSlicer

git remote get-url upstream >/dev/null 2>&1 || git remote add upstream "$UPSTREAM"
git config remote.upstream.tagOpt --no-tags
for t in "$FROM" "$TO"; do
  git fetch --depth 1 upstream "refs/tags/$t:refs/upstream-tags/$t"
done

PATCH=$(mktemp)
git diff --binary "refs/upstream-tags/$FROM" "refs/upstream-tags/$TO" > "$PATCH"
echo "diff $FROM -> $TO: $(grep -c '^diff --git' "$PATCH") ficheros"
if [[ "$MODE" == "--check" ]]; then
  git apply --check --3way "$PATCH" && echo "se aplica sin conflictos"
else
  git apply --3way "$PATCH" || { echo "Hay conflictos: resuélvelos (git status) antes de hacer commit."; exit 1; }
  echo "Aplicado. Revisa, compila y haz commit: 'Actualiza a PrusaSlicer $TO'"
fi
rm -f "$PATCH"
