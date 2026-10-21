"""Product entry points must not regress into hidden whole-card work."""
from pathlib import Path
import unittest
from test_rickyos_storage_layout import body

ROOT = Path(__file__).resolve().parents[2]


class LibrarySourceWorkflowTest(unittest.TestCase):
    def test_library_entry_and_paging_do_not_build_or_parse_covers(self):
        source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()
        for signature in ('void LibraryListActivity::onEnter()', 'void LibraryListActivity::buildRickyShelf('):
            method = body(source, signature)
            self.assertNotIn('buildLibraryIndex(', method)
            self.assertNotIn('ensureThumbnail(', method)
        self.assertNotIn('findMatchingBookForPath(', source)
        self.assertIn('ACTION_ADD', source)

    def test_storage_does_not_rebuild_library_or_automatically_count(self):
        source = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        self.assertNotIn('buildLibraryIndex(', source)
        self.assertNotIn('startStorageScan(', body(source, 'void RickyStorageActivity::onEnter()'))

    def test_directory_labels_do_not_enumerate_children(self):
        source = (ROOT / 'src/activities/home/FileBrowserActivity.cpp').read_text()
        method = body(source, 'void FileBrowserActivity::updateMetadata()')
        self.assertNotIn('openNextFile(', method)
        self.assertNotIn('metadataDirectory_', source)
        self.assertIn('PickLibraryBook', source)
        self.assertIn('PickLibraryFolder', source)
        self.assertIn('EditAction::AddToLibrary', source)

    def test_counts_only_update_does_not_report_sd_card_failure(self):
        source = (ROOT / 'src/activities/home/RickyStorageActivity.cpp').read_text()
        self.assertIn('bool attempted = false', source)
        self.assertIn('scan.attempted', body(source, 'void RickyStorageActivity::applyLastScan()'))
        self.assertIn('Space::NotMeasured', body(source, 'void RickyStorageActivity::applyLastScan()'))
        self.assertIn('scan.attempted = true', body(source, 'void scanStorageTask(void*)'))

    def test_import_joins_worker_and_restores_rules_when_cancelled(self):
        source = (ROOT / 'src/activities/library/LibrarySourcesActivity.cpp').read_text()
        exit_method = body(source, 'void LibraryImportActivity::onExit()')
        self.assertIn('cancel.store(true)', exit_method)
        self.assertIn('while (!done.load())', exit_method)
        self.assertNotIn('vTaskDelete', exit_method)
        self.assertIn('finishUpdate(ok,', source)

    def test_category_filter_is_separate_and_not_recomputed_during_paging(self):
        source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()
        method = body(source, 'void LibraryListActivity::applyFilter()')
        self.assertIn('categoryFilter', method)
        self.assertIn('readingFilter', method)
        self.assertIn('readPathHash', method)
        self.assertIn('std::binary_search', method)
        self.assertNotIn('loadHashes', body(source, 'void LibraryListActivity::buildRickyShelf('))

    def test_toolbar_fits_fixed_handler_budget(self):
        source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()
        entry = body(source, 'void LibraryListActivity::onEnter()')
        # UiList/UiTab consume two of UiAppHost's six slots.
        self.assertLessEqual(entry.count('app.on('), 4)
        shelf = body(source, 'void LibraryListActivity::buildRickyShelf(')
        for action in ('ManageCategories', 'BatchSelect', 'SortTools'):
            self.assertIn(f'ACTION_ADD, {action}', shelf)

    def test_cancel_category_assignment_is_explicit_and_preserves_batch(self):
        source = (ROOT / 'src/activities/library/LibraryCategoriesActivity.cpp').read_text()
        cancel = body(source, 'void LibraryCategoriesActivity::onBackButton()')
        self.assertIn('isCancelled = true', cancel)
        self.assertNotIn('categories.assign', cancel)
        shelf = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()
        self.assertIn('if (!result.isCancelled) clearBatch()', shelf)


if __name__ == '__main__':
    unittest.main()
