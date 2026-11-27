"use strict";

const width = 6;
const height = 5;
const walls = new Set([7, 8, 10, 13, 22]);
const target = 29;
const moves = {ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1]};
const board = document.querySelector("#board");
const status = document.querySelector("#status");
let position, relays, energy, won;

function render() {
  const tiles = [];
  for (let index = 0; index < width * height; index++) {
    const tile = document.createElement("span");
    tile.className = "cell";
    if (walls.has(index)) tile.classList.add("wall");
    if (index === target) { tile.classList.add("goal"); tile.textContent = "◎"; }
    if (relays.has(index)) { tile.classList.add("relay"); tile.textContent = "◇"; }
    if (index === position) { tile.classList.add("player"); tile.textContent = "◆"; }
    tiles.push(tile);
  }
  board.replaceChildren(...tiles);
  board.dataset.position = String(position);
  board.setAttribute("aria-label", `Raster. Signal in Zeile ${Math.floor(position / width) + 1}, Spalte ${position % width + 1}.`);
  status.textContent = won ? "Signal verbunden. Du hast gewonnen!" :
    energy === 0 ? "Energie aufgebraucht. Versuche eine kürzere Route." :
    `Relais ${3 - relays.size}/3 · Energie ${energy}`;
}

function reset() {
  position = 0;
  relays = new Set([4, 18, 26]);
  energy = 24;
  won = false;
  render();
}

function move(key) {
  if (!Object.hasOwn(moves, key) || won || energy === 0) return;
  const [dx, dy] = moves[key];
  const x = (position % width) + dx;
  const y = Math.floor(position / width) + dy;
  const next = y * width + x;
  if (x < 0 || x >= width || y < 0 || y >= height || walls.has(next)) return;
  position = next;
  energy--;
  relays.delete(next);
  won = position === target && relays.size === 0;
  render();
}

document.querySelector("#reset").addEventListener("click", reset);
document.querySelectorAll("[data-move]").forEach(button =>
  button.addEventListener("click", () => move(button.dataset.move)));
window.addEventListener("keydown", event => {
  if (!Object.hasOwn(moves, event.key)) return;
  event.preventDefault();
  move(event.key);
});

window.addEventListener("message", event => {
  if (event.source === window.parent && event.data?.type === "akeno-direction") move(event.data.key);
});
reset();
