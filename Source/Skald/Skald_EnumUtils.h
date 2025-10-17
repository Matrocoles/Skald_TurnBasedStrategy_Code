#pragma once

#include "CoreMinimal.h"

namespace Skald::EnumUtils {
inline bool HasMetaData(const UEnum* Enum, FName Key, int32 Index) {
#if WITH_METADATA
  return Enum && !Enum->GetMetaData(Key, Index).IsEmpty();
#else
  return false;
#endif
}

inline bool IsHiddenEntry(const UEnum* Enum, int32 Index) {
  return HasMetaData(Enum, TEXT("Hidden"), Index) ||
         HasMetaData(Enum, TEXT("BlueprintHidden"), Index) ||
         HasMetaData(Enum, TEXT("HiddenByDefault"), Index);
}
} // namespace Skald::EnumUtils
