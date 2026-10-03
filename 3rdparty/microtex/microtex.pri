# MicroTeX (MIT), vendored with small Qt 6 patches; see microtex-qt6.patch.
MICROTEX_DIR = $$PWD

# As system headers, so their warnings stay out of our build output.
QMAKE_CXXFLAGS += -isystem $$MICROTEX_DIR/src -isystem $$MICROTEX_DIR/tinyxml2
DEFINES += BUILD_QT
RESOURCES += $$MICROTEX_DIR/microtex_fonts.qrc

# Third-party code: compile it with warnings off instead of burying ours.
MICROTEX_SOURCES = $$files($$MICROTEX_DIR/src/*.cpp, true) $$MICROTEX_DIR/tinyxml2/tinyxml2.cpp
microtex.input = MICROTEX_SOURCES
microtex.output = ${OBJECTS_DIR}microtex_${QMAKE_FILE_BASE}$${first(QMAKE_EXT_OBJ)}
microtex.commands = $$QMAKE_CXX -c $(CXXFLAGS) -w $(INCPATH) ${QMAKE_FILE_IN} -o ${QMAKE_FILE_OUT}
microtex.dependency_type = TYPE_C
microtex.variable_out = OBJECTS
microtex.name = microtex ${QMAKE_FILE_IN}
QMAKE_EXTRA_COMPILERS += microtex
