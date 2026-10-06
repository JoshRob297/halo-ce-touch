package com.halo.decomp;

/** Standalone geometry/persistence regression checks; no phone required. */
public final class TouchLayoutTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    public static void main(String[] args) {
        TouchLayout layout = new TouchLayout();
        check(layout.size() == 19, "All 18 buttons and the movement stick must be editable");
        check(layout.type(TouchLayout.CAMERA) == TouchLayout.CAMERA
                && layout.x(TouchLayout.CAMERA) == 660 && layout.y(TouchLayout.CAMERA) == 36,
              "The camera mode button must sit on the top row");
        check(layout.x(TouchLayout.LEFT) == 115 && layout.y(TouchLayout.FIRE_LEFT) == 239,
              "Existing layouts must start with the original controls");
        layout.move(TouchLayout.LEFT, -500, -500);
        check(layout.x(TouchLayout.LEFT) >= 64 && layout.y(TouchLayout.LEFT) == 64,
              "Dragging must keep the full stick inside the screen without a toolbar exclusion");
        layout.move(TouchLayout.FIRE_LEFT, 10000, 10000);
        check(layout.x(TouchLayout.FIRE_LEFT)+39 <= 960 && layout.y(TouchLayout.FIRE_LEFT)+39 <= 540,
              "Dragging beyond the bottom/right edge must keep the stick reachable");
        layout.move(0, 230, 320);
        layout.move(5, 440, 390);
        TouchLayout reopened = new TouchLayout();
        for (int i = 0; i < layout.size(); i++) reopened.restore(i, layout.x(i), layout.y(i));
        for (int i = 0; i < layout.size(); i++) {
            check(layout.x(i) == reopened.x(i) && layout.y(i) == reopened.y(i),
                  "Saved coordinates must round-trip for control "+i);
        }
        float before = reopened.x(0);
        reopened.restore(0, Float.NaN, 200);
        reopened.restore(0, Float.POSITIVE_INFINITY, 200);
        reopened.restore(0, -20, 200);
        check(reopened.x(0) == before, "Invalid saved coordinates must not corrupt the layout");
        reopened.move(0, Float.NaN, 200);
        check(reopened.x(0) == before, "Invalid drag coordinates must not corrupt the layout");
        // A saved top-row Pause button is valid even before it has been moved.
        check(reopened.y(10) == 36, "Reopening must preserve unmoved top-row controls");
        reopened.bounds(1200, 540);
        reopened.move(0, 9999, 1);
        check(reopened.x(0) == 1164 && reopened.y(0) == 36,
              "Controls must reach the full widescreen edge and top edge");
        TouchLayout wide = new TouchLayout();
        wide.restore(0, reopened.savedX(0), reopened.savedY(0));
        wide.bounds(1200, 540);
        check(wide.x(0) == reopened.x(0), "Widescreen positions must survive reload");
        // Duplicates retain their action and are independently placed and hidden.
        int copy = wide.duplicate(4);
        check(copy == 19 && wide.type(copy) == 4, "Fire copies must retain the fire action");
        wide.setShown(4, false);
        wide.move(copy, 300, 100);
        check(!wide.shown(4) && wide.shown(copy), "Hiding an original must not hide its copy");
        check(wide.duplicate(TouchLayout.LEFT) == -1, "There can only be one movement stick");
        String exported = wide.exportConfiguration(2.25f);
        TouchLayout.Configuration imported = TouchLayout.importConfiguration(exported);
        imported.layout.bounds(1200, 540);
        check(imported.sensitivity == 2.25f && imported.layout.size() == 20,
              "Configuration must preserve sensitivity and duplicates");
        for (int i = 0; i < wide.size(); i++) {
            check(imported.layout.type(i) == wide.type(i) && imported.layout.shown(i) == wide.shown(i)
                && Math.abs(imported.layout.x(i)-wide.x(i)) < 0.001f
                && Math.abs(imported.layout.y(i)-wide.y(i)) < 0.001f,
                "Export/import must preserve each control on widescreen");
        }
        check(wide.add(4) == 4 && wide.shown(4), "Add must restore a hidden button first");
        wide.setShown(TouchLayout.LEFT, false);
        check(wide.add(TouchLayout.LEFT) == TouchLayout.LEFT, "Hidden move stick must be restorable");
        reject(exported.replace("version=3", "version=9"));
        reject(exported.replace("control.19.type=4", "control.19.type=16"));
        reject(exported.replace("control.19.x=240.0", "control.19.x=NaN"));
        reject(exported.replace("count=20", "count=10000"));
        reject(exported.replace("sensitivity=2.25", "sensitivity=Infinity"));
        reject(exported.replace("control.0.visible=false", "control.0.visible=maybe")
            .replace("control.0.visible=true", "control.0.visible=maybe"));
        reject("not a layout");
        wide.setSize(copy, 1.6f);
        wide.rumbleEnabled = false; wide.gyroscopeEnabled = true;
        wide.fpsCounter = true; wide.fieldOfView = 80f;
        String sizedText = wide.exportConfiguration(2.25f);
        TouchLayout.Configuration sized = TouchLayout.importConfiguration(sizedText);
        check(sized.layout.sizeScale(copy) == 1.6f && !sized.layout.rumbleEnabled && sized.layout.gyroscopeEnabled,
              "Sizes, rumble and gyro settings must survive export/import");
        check(sized.layout.fpsCounter && Math.abs(sized.layout.fieldOfView-80f) < 0.001f,
              "The FPS counter and the field of view must survive export/import");
        int sizedCopy = wide.duplicate(copy);
        check(wide.sizeScale(sizedCopy) == 1.6f, "Duplicates inherit their source size");
        wide.move(copy, 99999, 99999);
        wide.setSize(copy, 2f);
        check(wide.x(copy)+wide.radius(copy) <= 1200 && wide.y(copy)+wide.radius(copy) <= 540,
              "Growing a button at the edge must keep it reachable");
        reject(sizedText.replace("control.19.size=1.6", "control.19.size=NaN"));
        reject(sizedText.replace("control.19.size=1.6", "control.19.size=3.0"));
        reject(sizedText.replace("rumble=false", "rumble=invalid"));
        reject(sizedText.replace("gyroscope=true", "gyroscope=invalid"));
        reject(sizedText.replace("field-of-view=80.0", "field-of-view=120.0"));
        reject(sizedText.replace("field-of-view=80.0", "field-of-view=NaN"));
        // A version 1 file predates the sizes and the General settings.
        String legacy = sizedText.replace("version=3", "version=1").replaceAll("(?m)^.*\\.size=.*\\R", "")
            .replaceAll("(?m)^(rumble|gyroscope|fps-counter|field-of-view)=.*\\R", "");
        TouchLayout.Configuration old = TouchLayout.importConfiguration(legacy);
        check(old.layout.sizeScale(copy) == 1 && old.layout.rumbleEnabled && !old.layout.gyroscopeEnabled,
              "Legacy layouts must load with default sizes and gyro disabled");
        check(!old.layout.fpsCounter && old.layout.fieldOfView == 70f,
              "Legacy layouts must default the FPS counter and the field of view");
        // A file from before the camera button existed gains it at its default.
        TouchLayout reference = new TouchLayout();
        StringBuilder oldText = new StringBuilder(
            "format=halo-touch-layout\nversion=2\nrumble=true\ngyroscope=false\ncount=18\nsensitivity=2.0\n");
        for (int i = 0; i < 18; i++) {
            oldText.append("control.").append(i).append(".type=").append(i).append('\n');
            oldText.append("control.").append(i).append(".x=").append(reference.savedX(i)).append('\n');
            oldText.append("control.").append(i).append(".y=").append(reference.savedY(i)).append('\n');
            oldText.append("control.").append(i).append(".visible=true\n");
            oldText.append("control.").append(i).append(".size=1.0\n");
        }
        TouchLayout.Configuration upgraded = TouchLayout.importConfiguration(oldText.toString());
        check(upgraded.layout.size() == 19
                && upgraded.layout.type(TouchLayout.CAMERA) == TouchLayout.CAMERA
                && upgraded.layout.x(TouchLayout.CAMERA) == 660 && upgraded.layout.y(TouchLayout.CAMERA) == 36,
              "A layout from before the camera control must gain it at its default position");
        check(!upgraded.layout.fpsCounter && upgraded.layout.fieldOfView == 70f,
              "A version 2 layout must default the FPS counter and the field of view");
        wide.resetDefaults();
        check(wide.size() == 19 && wide.shown(4) && wide.x(4) == 915*1200f/960,
              "Reset restores defaults on the current display and removes all copies");
        check(wide.sizeScale(4) == 1f && !wide.rumbleEnabled && wide.gyroscopeEnabled,
              "Button reset must restore sizes without changing General settings");
        while (wide.duplicate(4) >= 0) {}
        check(wide.size() == TouchLayout.MAX_CONTROLS, "Duplicate count must be bounded");
        System.out.println("Touch layout, visibility, duplication and import/export checks passed");
    }

    private static void reject(String text) {
        try { TouchLayout.importConfiguration(text); }
        catch (IllegalArgumentException expected) { return; }
        throw new AssertionError("Invalid configuration was accepted");
    }
}
