"use strict";

const frame = document.querySelector("iframe");
let previous = "";

function readController() {
  const pad = Array.from(navigator.getGamepads?.() ?? []).find(item => item?.connected);
  let key = "";
  if (pad) {
    if (pad.buttons[14]?.pressed || pad.axes[0] < -.55) key = "ArrowLeft";
    else if (pad.buttons[15]?.pressed || pad.axes[0] > .55) key = "ArrowRight";
    else if (pad.buttons[12]?.pressed || pad.axes[1] < -.55) key = "ArrowUp";
    else if (pad.buttons[13]?.pressed || pad.axes[1] > .55) key = "ArrowDown";
  }
  if (key && key !== previous) frame.contentWindow.postMessage({type: "akeno-direction", key}, "*");
  previous = key;
  requestAnimationFrame(readController);
}

requestAnimationFrame(readController);
