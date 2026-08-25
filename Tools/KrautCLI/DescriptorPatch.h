#pragma once

// Typed descriptor patching for the CLI: apply `--set <BranchType>.<Field>=<Value>`
// overrides to a loaded TreeFile, and dump all patchable fields for inspection.
//
// Only scalar fields of Kraut::SpawnNodeDesc are patchable (curves and textures
// are inherited from the source descriptor). Enum fields accept names or integers.

#include "TreeFile.h"

namespace KrautCLI
{
  // Applies one override of the form "<BranchType>.<Field>=<Value>".
  // Branch type names: Trunk_1..3, Main_Branches_1..3, Sub_Branches_1..3, Twigs_1..3
  // (underscore optional, case-insensitive).
  // Returns false and sets out_sError on unknown type/field or bad value.
  bool ApplyDescriptorPatch(TreeFile& treeFile, const char* szPatch, aeString& out_sError);

  // Copies the full SpawnNodeDesc of one branch type into another slot
  // (including curves and textures), fixing up the stored type id.
  // Useful to activate a previously unused type (e.g. Sub_Branches_1) with sane defaults.
  bool CopyBranchTypeDesc(TreeFile& treeFile, const char* szFromType, const char* szToType, aeString& out_sError);

  // Prints every patchable field of every branch type as "Type.Field = Value" lines.
  // The output can be fed back into --set (after editing values).
  void DumpDescriptorFields(const TreeFile& treeFile);
} // namespace KrautCLI
