#pragma once

#include "CoreMinimal.h"
#include "UObject/Enum.h"

namespace Skald::EnumUtils {
inline bool HasMetaData(const UEnum* Enum, FName Key, int32 Index) {
#if WITH_METADATA
  if (!Enum) {
    return false;
  }

  const FString KeyString = Key.ToString();
  return !Enum->GetMetaData(*KeyString, Index).IsEmpty();
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
