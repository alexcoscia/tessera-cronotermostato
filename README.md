# Cronotermostato

Tile for the cronotermostato Home Assistant integration: the target temperature of a room with plus and minus keys,
and a bar with today's bands of its weekly program.

> This is a spike version used to try the plugin API (see SPIKE.md). It is not the finished tile.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Room program** tile and choose the room's climate entity.
3. In the inspector, choose the room's program sensor.

## Good to know

- It needs the cronotermostato integration in Home Assistant.
