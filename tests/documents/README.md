# Document and UBZ import regression checks

Run `tests\documents\run-tests.cmd` from a repository checkout on the configured Windows development machine. The source root defaults to `../..` relative to the test directory; QuaZip defaults to `../OpenBoard-ThirdParty` relative to that source root. It uses Qt 6.9.3/MSVC 2022 and the same QuaZip sources/zlib as OpenBoard. No OpenBoard application, Office process, installer, or real user document is opened. Every document/archive fixture lives in a `QTemporaryDir` and is removed by its owning test.

To run a staged copy of the tests against another checkout, pass its source root as the first argument, and optionally the third-party root as the second argument:

```bat
run-tests.cmd C:\OpenBoard-src\OpenBoard-1.7.7 C:\OpenBoard-src\OpenBoard-ThirdParty
```

The equivalent qmake parameters are `REPO=...` and `THIRDPARTY=...`. The runner sets `DOCUMENT_TEST_SOURCE` and `DOCUMENT_TEST_DIRECTORY` for source-parity checks. Qt/MSVC defaults can be overridden with the `DOCUMENT_TEST_QT` and `DOCUMENT_TEST_VCVARS` environment variables. Build output and QtTest reports are excluded by the local `.gitignore`.

## What is exercised

- The real, complete `UBImportDocument.cpp`, `UBDocument.cpp`, `UBThumbnailScene.cpp`, and `UBBackgroundLoader.cpp` are compiled into the test executable.
- The real `UBPersistenceManager::addDirectoryContentToDocument` implementation is included from `persistence-method.inc`. `productionMethodIsExact` compares it, including all statements, with the production source before other tests. This isolates one method from the application's singleton initialization; it is not an independent reimplementation of the method.
- Real Qt file I/O, QSaveFile, QGraphicsScene, background threads, QuaZip, zlib, and ZIP CRC checking run. Window/controller services, SVG UUID rewriting, bitmap rendering, metadata scheduling, and selected persistence collaborators are test doubles. File-copy and save failures are deliberately injected.
- The suite covers directory traversal/absolute/UNC/drive/alternate-stream/device paths, case and separator aliases, duplicate names and file-directory conflicts, normal directory records and Chinese names, a streamed member larger than 2 MiB, and cleanup after CRC failure.
- It also covers rename persistence and rollback, cold-destination page copying, failed copy state, duplicate/move/delete/named insertion, UBZ append counts/names/thumbnail slots, legacy UBZs without names, protection of an existing uncounted page, dependency-copy failure, and rollback after a partial page-copy failure. Unsupported same-document forward copies are checked for completely unchanged counts, names, thumbnail identities/indexes/labels, and page files.

## Latest result

2026-09-13: **37 passed, 0 failed, 0 skipped**, 664 ms, using the repository source-root override after integration. The report is written to ignored `results.txt` when the runner is used.

The successful append test processes events after appending so the actual background loader indexes the new thumbnail slots; it then creates another page and verifies imported page files were not overwritten.

## Boundaries

This is an isolated component regression suite, not a full application/UI end-to-end test. It does not validate real SVG rendering or PDF/PowerPoint/LibreOffice conversions, a real filesystem reaching disk-full, permissions on a production document directory, UI dialogs with a real display, exported UBZ compatibility across other OpenBoard versions, or performance of very large page collections. Imported page names currently reuse per-page saving; importing many named pages can perform quadratic sidecar writes. Copying dependencies may leave harmless unreferenced assets on an I/O failure; existing assets and page files are not overwritten by rollback.

If the production append method changes, regenerate `persistence-method.inc` directly from that method and review the diff. Do not hand-edit the extracted method to make a test pass.
