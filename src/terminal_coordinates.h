#pragma once

#include <cstddef>

namespace imterm {

	// Coordinates in the active terminal viewport. Both values are zero-based.
	struct ScreenPosition {
		int mRow = 0;
		int mColumn = 0;
	};

	// A rendered column counts terminal cells. It is not a UTF-8 byte offset.
	struct RenderedColumn {
		int mValue = 0;
	};

	// A row in the complete scrollback buffer plus its rendered column.
	struct BufferPosition {
		size_t mRow = 0;
		RenderedColumn mColumn;
	};

	// A byte offset into the UTF-8 byte storage of one terminal line.
	struct ByteOffset {
		size_t mValue = 0;
	};

	// Counts, rather than maximum zero-based coordinates.
	struct ViewportSize {
		int mRows = 1;
		int mColumns = 1;
	};

}
