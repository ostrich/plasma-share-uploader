import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as SystemDialogs
import QtTest
import ShareUploader
import ShareUploaderTests

Item {
    id: root

    height: 860
    width: 1180

    TestCase {
        id: tests

        property var comp
        property var win

        function bounds(obj) {
            const point = obj.mapToItem(win.contentItem, 0, 0);
            return [point.x, point.y, obj.width, obj.height];
        }
        function cleanup() {
            if (win)
                win.destroy();

            if (comp)
                comp.destroy();

            wait(0);
            Fixture.clear();
        }
        function edit(name, value) {
            const obj = item(name);
            obj.forceActiveFocus();
            obj.selectAll();
            Fixture.typeText(obj, value);
        }
        function init() {
            Fixture.reset();
            comp = Qt.createComponent("qrc:/qt/qml/ShareUploader/ManagerWindow.qml");
            compare(comp.status, Component.Ready, comp.errorString());
            win = comp.createObject(null, {
                "controller": Fixture.manager
            });
            verify(win !== null);
            win.requestActivate();
            tryCompare(win, "visible", true);
            waitForRendering(win.contentItem);
        }
        function item(name) {
            const obj = findChild(win, name) || Fixture.findItem(win, name);
            verify(!!obj, "Object exists");
            return obj;
        }
        function tab(index) {
            const tabs = item("editorTabs");
            mouseClick(tabs.itemAt(index));
            waitForRendering(win.contentItem);
        }
        function test_addPreprocessingRule() {
            tab(4);
            mouseClick(item("preRules.add"));
            tryVerify(function () {
                return Fixture.manager.draft.document.preUpload.length === 1;
            });
            compare(Fixture.manager.draft.document.preUpload[0].commands[0].argv[0], "exiv2");
        }
        function test_closeWindowCleanAndDirty() {
            win.close();
            tryCompare(win, "visible", false);
            win.destroy();
            win = comp.createObject(null, {
                controller: Fixture.manager
            });
            verify(win !== null);
            waitForRendering(win.contentItem);
            edit("displayName", "Save before closing");
            win.close();
            const discard = item("discardDialog");
            tryCompare(discard, "opened", true);
            mouseClick(discard.standardButton(Dialog.Cancel));
            tryCompare(discard, "visible", false);
            verify(win.visible);
            verify(Fixture.manager.dirty);
            win.close();
            tryCompare(discard, "opened", true);
            mouseClick(discard.standardButton(Dialog.Save));
            tryCompare(win, "visible", false);
            compare(JSON.parse(Fixture.saved("a-valid.json")).displayName, "Save before closing");
        }
        function test_credentialInputClearsOnSelection() {
            tab(2);
            edit("credentialSecret", "transient-test-secret");
            verify(!Fixture.manager.dirty);
            verify(!Fixture.manager.draft.rawText.includes("transient-test-secret"));
            Fixture.manager.request("select", Fixture.path("b-fields.json"));
            tryCompare(item("credentialSecret"), "text", "");
        }
        function test_editAcceptedTypesAndExtensions() {
            mouseClick(item("accept.mimeTypes.add"));
            edit("accept.mimeTypes.value.0", "image/png");
            mouseClick(item("accept.mimeTypes.add"));
            edit("accept.mimeTypes.value.1", "image/jpeg");
            mouseClick(item("accept.extensions.add"));
            edit("accept.extensions.value.0", ".PNG");
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
            const json = JSON.parse(Fixture.saved("a-valid.json"));
            compare(json.schemaVersion, 1);
            compare(json.accept.mimeTypes, ["image/png", "image/jpeg"]);
            compare(json.accept.extensions, [".PNG"]);
            verify(json.constraints === undefined);
            verify(json.pluginTypes === undefined);
        }
        function test_editAndSave() {
            edit("displayName", "Edited name 123 + !");
            tryCompare(Fixture.manager, "dirty", true);
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
            const json = JSON.parse(Fixture.saved("a-valid.json"));
            compare(json.displayName, "Edited name 123 + !");
            compare(json.future, [true, 42]);
        }
        function test_errorIconColor() {
            verify(Fixture.redIcon(win));
            verify(Fixture.screenshot(win, "error-normal"));
            Fixture.manager.request("select", Fixture.path("b-fields.json"));
            waitForRendering(win.contentItem);
            verify(Fixture.redIcon(win));
            verify(Fixture.screenshot(win, "error-selected"));
        }
        function test_exportReviewAndFileDialog() {
            const file = item("exportFileDialog");
            file.options = SystemDialogs.FileDialog.DontUseNativeDialog;
            mouseClick(item("exportTarget"));
            const review = item("exportReview");
            tryCompare(review, "opened", true);
            mouseClick(review.standardButton(Dialog.Save));
            tryCompare(review, "visible", false);
            tryCompare(file, "visible", true);
            verify(file.selectedFile.toString().startsWith("file:"));
            verify(!file.selectedFile.toString().match(/^file:\/\/\/[^/]+$/));
            // Drive the dialog's filename control: selectedFile is only the
            // initial selection once the native/fallback dialog has opened.
            file.currentFolder = Fixture.transferFile().toString().replace(/[^/]+$/, "");
            edit("fileNameTextField", "export.json");
            const buttonBox = item("fileNameTextField").parent.children.find(child => child.standardButtons !== undefined);
            verify(!!buttonBox);
            mouseClick(buttonBox.standardButton(Dialog.Open));
            tryVerify(function () {
                return Fixture.exported().length > 0;
            }, 5000, "Export selection: " + file.selectedFile + ", message: " + Fixture.manager.message);
            compare(JSON.parse(Fixture.exported()).future, [true, 42]);
        }
        function test_initialMalformedDraftShowsJson() {
            win.destroy();
            wait(0);
            Fixture.manager.request("select", Fixture.path("d-json.json"));
            win = comp.createObject(null, {
                controller: Fixture.manager
            });
            verify(win !== null);
            waitForRendering(win.contentItem);
            verify(item("targetJson").visible);
            edit("targetJson", Fixture.saved("a-valid.json"));
            tryCompare(Fixture.manager.draft, "hasObject", true);
        }
        function test_invalidMapSurvivesTabs() {
            tab(1);
            mouseClick(item("request.headers.add"));
            edit("request.headers.name.0", "X-Test");
            edit("request.headers.value.0", "first");
            mouseClick(item("request.headers.add"));
            edit("request.headers.name.1", "X-Test");
            edit("request.headers.value.1", "second");
            tryVerify(function () {
                return Fixture.manager.status.startsWith("Needs attention");
            });
            tab(0);
            tab(1);
            compare(item("request.headers.value.1").text, "second");
            edit("request.headers.name.1", "X-Other");
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
            compare(JSON.parse(Fixture.saved("a-valid.json")).request.headers["X-Other"], "second");
        }
        function test_jsonBodyKeepsIncompleteText() {
            Fixture.manager.draft.setBodyType("json");
            Fixture.manager.draft.jsonBody = "{\"value\":42}";
            tab(1);
            const body = item("jsonBody");
            mouseClick(body);
            tryCompare(body, "text", "{\"value\":42}");
            edit("jsonBody", "[false,");
            tab(0);
            tab(1);
            compare(body.text, "[false,");
            verify(Fixture.manager.status.startsWith("Needs attention"));
            edit("jsonBody", "[false, 1, null]");
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
            compare(JSON.parse(Fixture.saved("a-valid.json")).request.body.value, [false, 1, null]);
        }
        function test_offlineParsing() {
            tab(6);
            edit("testResponse", "https://files.example/offline");
            mouseClick(item("testResponseParsing"));
            tryVerify(function () {
                return Fixture.manager.test.diagnostics.includes("https://files.example/offline");
            });
            compare(Fixture.requestCount(), 0);
        }
        function test_pagesRender() {
            for (const size of [
                {
                    "tag": "normal",
                    "w": 1180,
                    "h": 860
                },
                {
                    "tag": "compact",
                    "w": 960,
                    "h": 720
                }
            ]) {
                win.width = size.w;
                win.height = size.h;
                for (let i = 0; i < 7; ++i) {
                    tab(i);
                    verify(Fixture.screenshot(win, "manager-" + size.tag + "-" + i));
                }
            }
            compare(Fixture.manager.draft.rawText, Fixture.saved("a-valid.json"));
        }
        function test_rawJsonRepair() {
            tab(5);
            edit("targetJson", "{broken");
            tryCompare(Fixture.manager.draft, "hasObject", false);
            edit("targetJson", Fixture.saved("a-valid.json"));
            tryCompare(Fixture.manager.draft, "hasObject", true);
            tab(0);
            edit("displayName", "Repaired");
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
        }
        function test_secondRuleAndCommandKeepSelection() {
            Fixture.manager.draft.setValue(["preUpload"], [
                {
                    "mime": ["image/png"],
                    "fileHandling": "inplace_copy",
                    "commands": [
                        {
                            "argv": ["first", "${FILE}"]
                        }
                    ]
                },
                {
                    "mime": ["image/jpeg"],
                    "fileHandling": "inplace_copy",
                    "commands": [
                        {
                            "argv": ["second", "${FILE}"]
                        },
                        {
                            "argv": ["third", "${FILE}"]
                        }
                    ]
                }
            ]);
            compare(Fixture.manager.draft.document.preUpload.length, 2);
            tab(4);
            mouseClick(item("preRules.row.1"));
            edit("preUpload.1.mime.value.0", "image/webp");
            tryCompare(item("preUpload.1.mime.value.0"), "text", "image/webp");
            mouseClick(item("preCommands.row.1"));
            const args = item("preUpload.1.commands.1.argv");
            // Bring the argument table into view in the scrolling form.
            args.forceActiveFocus();
            edit("preUpload.1.commands.1.argv.value.0", "edited-third");
            compare(Fixture.manager.draft.document.preUpload[1].commands[1].argv[0], "edited-third");
            compare(Fixture.manager.draft.document.preUpload[0].commands[0].argv[0], "first");
        }
        function test_sidebarKeyboardUsesDraftGuard() {
            const list = item("targetList");
            Fixture.pressKey(list, Qt.Key_Down);
            tryCompare(Fixture.manager, "selectedPath", Fixture.path("b-fields.json"));
            Fixture.pressKey(list, Qt.Key_Up);
            tryCompare(Fixture.manager, "selectedPath", Fixture.path("a-valid.json"));
            edit("displayName", "Unsaved keyboard edit");
            Fixture.pressKey(list, Qt.Key_Down);
            const dialog = item("discardDialog");
            tryCompare(dialog, "opened", true);
            mouseClick(dialog.standardButton(Dialog.Cancel));
            tryCompare(Fixture.manager, "selectedPath", Fixture.path("a-valid.json"));
        }
        function test_stableLayout(data) {
            win.width = data.width;
            win.height = data.height;
            waitForRendering(win.contentItem);
            const frame = bounds(item("editorFrame"));
            const status = bounds(item("targetStatus"));
            for (const name of ["b-fields.json", "c-long.json", "d-json.json", "a-valid.json"]) {
                Fixture.manager.request("select", Fixture.path(name));
                waitForRendering(win.contentItem);
                compare(bounds(item("editorFrame")), frame);
                compare(bounds(item("targetStatus")), status);
            }
            Fixture.externalChange();
            tryVerify(function () {
                return Fixture.manager.diagnosticText.includes("changed on disk");
            });
            compare(bounds(item("editorFrame")), frame);
            compare(bounds(item("targetStatus")), status);
            verify(Fixture.screenshot(win, "layout-" + data.tag));
        }
        function test_stableLayout_data() {
            return [
                {
                    "tag": "normal",
                    "width": 1180,
                    "height": 860
                },
                {
                    "tag": "compact",
                    "width": 960,
                    "height": 720
                }
            ];
        }
        function test_unsavedDialog() {
            edit("displayName", "Discard me");
            mouseClick(item("duplicateTarget"));
            const dialog = item("discardDialog");
            tryCompare(dialog, "opened", true);
            mouseClick(dialog.standardButton(Dialog.Cancel));
            tryCompare(Fixture.manager, "title", "Discard me *");
            tryCompare(dialog, "visible", false);
            mouseClick(item("duplicateTarget"));
            tryCompare(dialog, "opened", true);
            mouseClick(dialog.standardButton(Dialog.Discard));
            tryCompare(Fixture.manager, "title", "Catbox (copy) *");
            tryCompare(dialog, "visible", false);
            mouseClick(item("saveTarget"));
            tryCompare(Fixture.manager, "dirty", false);
        }
        function test_uploadThroughButton() {
            Fixture.startServer();
            tab(6);
            mouseClick(item("uploadTestFile"));
            tryVerify(function () {
                return Fixture.requestCount() === 1 && !Fixture.manager.test.busy;
            });
            verify(Fixture.manager.test.responseBody.includes("https://files.example/qml-test"));
            verify(Fixture.manager.test.diagnostics.includes("https://files.example/qml-test"));
        }

        name: "ManagerWindow"
        when: windowShown
    }
}
