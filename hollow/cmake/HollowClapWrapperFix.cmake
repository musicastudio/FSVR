# clap-wrapper's VST3 controller hands an edit made in the plug-in's own editor to the host with
# performEdit() but never stores it in its own parameter, so getParamNormalized() goes on returning the
# value from before the edit until the plug-in asks for a rescan. Cubase reads the controller around an
# edit and sends what it read back to the processor: a choice made in the editor (a performance, a
# voice bank) jumped back to the one before. A VST3 editor sets the controller's value first and then
# calls performEdit(), and this makes the wrapper do the same (wrapasvst3.cpp, ClapAsVst3::onIdle).
# v0.16.0 and the next-branch commit pinned above (fb9d6e4) both have it unchanged; drop this file
# once a release stores the value itself.
#
# A one-line replace rather than a patch file: Windows checkouts with core.autocrlf turn the fetched
# source into CRLF, which a patch's context lines would not match. Runs on every configure, idempotent.
function(hollow_fix_clap_wrapper dir)
  set(file "${dir}/src/wrapasvst3.cpp")
  file(READ "${file}" src)
  set(old "performEdit(param->getInfo().id, param->asVst3Value(v));")
  set(new "if (param) { param->setNormalized(param->asVst3Value(v)); performEdit(param->getInfo().id, param->asVst3Value(v)); }   // hollow: the controller holds the value before the host hears of it")
  string(FIND "${src}" "${new}" done)
  if(NOT done EQUAL -1)
    return()
  endif()
  string(FIND "${src}" "${old}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "clap-wrapper changed: ${file} has no '${old}' to fix. Check whether the new "
                        "version stores an editor's edit in its parameter, and drop or update ${CMAKE_CURRENT_FUNCTION_LIST_FILE}.")
  endif()
  string(REPLACE "${old}" "${new}" src "${src}")
  file(WRITE "${file}" "${src}")
  message(STATUS "hollow: clap-wrapper's VST3 controller now stores editor edits (${file})")
endfunction()
