# Static SDL builds have no runtime DLLs to copy.
if(RA2_RUNTIME_DLLS)
  string(REPLACE "|" ";" dlls "${RA2_RUNTIME_DLLS}")
  file(COPY ${dlls} DESTINATION "${RA2_RUNTIME_DIR}")
endif()
