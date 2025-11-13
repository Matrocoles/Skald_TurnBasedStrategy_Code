#include "SkaldToolTipWidget.h"

FVector2D USkaldToolTipWidget::NativeGetDesiredToolTipPosition(FVector2D MousePosition) const
{
    // Offset tooltips so they don’t overlap large custom faction cursors
    return MousePosition + FVector2D(60.f, 50.f);
}
