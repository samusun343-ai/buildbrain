# Material Icons — source assets for the brain frontend chrome

The SVG files in this directory are the official Google Material Icons
(filled "materialicons" style, 24px grid), downloaded 2026-11-20 from
the google/material-design-icons repository:

  back.svg    src/navigation/arrow_back/materialicons/24px.svg
  forward.svg src/navigation/arrow_forward/materialicons/24px.svg
  reload.svg  src/navigation/refresh/materialicons/24px.svg
  stop.svg    src/navigation/close/materialicons/24px.svg
  home.svg    src/action/home/materialicons/24px.svg
  menu.svg    src/navigation/more_vert/materialicons/24px.svg
  plus.svg    src/content/add/materialicons/24px.svg
  star.svg    src/toggle/star/materialicons/24px.svg

Material Icons are licensed under the Apache License 2.0
(https://github.com/google/material-design-icons/blob/master/LICENSE).

These files are build-time sources only: ../make_icons.py rasterises
them into the frontend's icons.h. They are not shipped in the browser.
