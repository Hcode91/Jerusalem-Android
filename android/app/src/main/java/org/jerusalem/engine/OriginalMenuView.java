package org.jerusalem.engine;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;

/** Original user-supplied artwork with real accessible controls at verified hit boxes. */
final class OriginalMenuView extends FrameLayout {
    private final Bitmap background;
    private final Bitmap[] highlights = new Bitmap[10];
    private final int[][] bounds = new int[10][];
    private final Button[] actions;
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final RectF imageRect = new RectF();
    private float scale, originX, originY;

    static Button action(Context context, String label) {
        Button button = new Button(context) {
            @Override protected void drawableStateChanged() {
                super.drawableStateChanged();
                if (getParent() instanceof View) ((View) getParent()).invalidate();
            }
        };
        button.setContentDescription(label);
        return button;
    }

    OriginalMenuView(Context context, int[][] packet, Button[] buttons) {
        super(context);
        if (packet == null || packet.length != 11 || buttons.length != 10)
            throw new IllegalArgumentException("Incomplete original menu artwork");
        background = bitmap(packet[0], 640, 480);
        if (background == null) throw new IllegalArgumentException("Missing original menu background");
        actions = buttons;
        for (int i = 0; i < 10; ++i) {
            int[] row = packet[i+1];
            if (row == null || row.length < 6 || row[0] < 0 || row[1] < 0 || row[2] > 640 || row[3] > 480 || row[0] >= row[2] || row[1] >= row[3])
                throw new IllegalArgumentException("Invalid original menu hit box");
            bounds[i] = new int[] {row[0], row[1], row[2], row[3]};
            highlights[i] = bitmap(row, 256, 40);
            Button button = buttons[i];
            button.setText(""); // The labels are part of the original artwork.
            button.setBackgroundColor(Color.TRANSPARENT);
            button.setStateListAnimator(null);
            button.setElevation(0);
            button.setPadding(0, 0, 0, 0);
            button.setMinWidth(0);button.setMinimumWidth(0);
            button.setMinHeight(0);button.setMinimumHeight(0);
            addView(button, new FrameLayout.LayoutParams(0, 0));
        }
        setWillNotDraw(false);
        setClipChildren(false);
        setBackgroundColor(Color.BLACK);
        setContentDescription("Original game menu");
        setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);
    }

    private static Bitmap bitmap(int[] row, int maxWidth, int maxHeight) {
        if (row == null || row.length < 6) throw new IllegalArgumentException("Invalid menu image packet");
        int width = row[4], height = row[5];
        if (width == 0 && height == 0 && row.length == 6) return null;
        if (width <= 0 || height <= 0 || width > maxWidth || height > maxHeight || (long) width*height != row.length-6)
            throw new IllegalArgumentException("Invalid menu image dimensions");
        return Bitmap.createBitmap(row, 6, width, width, height, Bitmap.Config.ARGB_8888);
    }

    @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        scale = Math.min((right-left)/640f, (bottom-top)/480f);
        originX = ((right-left)-640*scale)/2;
        originY = ((bottom-top)-480*scale)/2;
        imageRect.set(originX, originY, originX+640*scale, originY+480*scale);
        for (int i = 0; i < actions.length; ++i) {
            int[] b = bounds[i];
            actions[i].layout(Math.round(originX+b[0]*scale), Math.round(originY+b[1]*scale),
                    Math.round(originX+b[2]*scale), Math.round(originY+b[3]*scale));
        }
    }

    @Override protected void dispatchDraw(Canvas canvas) {
        canvas.drawBitmap(background, null, imageRect, paint);
        int saved = canvas.save();
        canvas.translate(originX, originY);canvas.scale(scale, scale);
        for (int i = 0; i < actions.length; ++i) {
            Button action = actions[i];int[] b = bounds[i];
            if (!action.isEnabled()) {
                paint.setColor(0x99000000);
                canvas.drawRect(b[0], b[1], b[2], b[3], paint);
            } else if (action.isPressed() || action.isFocused() || action.isHovered()) {
                if (highlights[i] != null) {paint.setColor(Color.WHITE);canvas.drawBitmap(highlights[i], b[0], b[1], paint);}
                else {
                    paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2);paint.setColor(0xFFFFE3AE);
                    canvas.drawRect(b[0]+1, b[1]+1, b[2]-1, b[3]-1, paint);
                    paint.setStyle(Paint.Style.FILL);
                }
            }
        }
        paint.setColor(Color.WHITE);
        canvas.restoreToCount(saved);
        super.dispatchDraw(canvas);
    }
}
