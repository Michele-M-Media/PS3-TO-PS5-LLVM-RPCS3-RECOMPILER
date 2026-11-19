// Shared helper implementations extracted from the local RPCS3 source; retain upstream license.
#include "stdafx.h"
#include "Utilities/Thread.h"
#include <QString>
#include <QTimer>
#include <QEventLoop>
#include <thread>
#include <chrono>
template <>
void fmt_class_string<QString>::format(std::string& out, u64 arg)
{
 	out += get_object(arg).toStdString();
}

extern void qt_events_aware_op(int repeat_duration_ms, std::function<bool()> wrapped_op)
{
	ensure(wrapped_op);

	if (thread_ctrl::is_main())
	{
		// NOTE:
		// I noticed that calling this from an Emu callback can cause the
		// caller to get stuck for a while during newly opened Qt dialogs.
		// Adding a timeout here doesn't seem to do anything in that case.
		QEventLoop* event_loop = nullptr;

		std::shared_ptr<std::function<void()>> check_iteration;
		check_iteration = std::make_shared<std::function<void()>>([&]()
		{
			if (wrapped_op())
			{
				event_loop->exit(0);
			}
			else
			{
				QTimer::singleShot(repeat_duration_ms, event_loop, *check_iteration);
			}
		});

		while (!wrapped_op())
		{
			// Init event loop
			event_loop = new QEventLoop();

			// Queue event initially
			QTimer::singleShot(0, event_loop, *check_iteration);

			// Event loop
			event_loop->exec();

			// Cleanup
			event_loop->deleteLater();
		}
	}
	else
	{
		while (!wrapped_op())
		{
			if (repeat_duration_ms == 0)
			{
				std::this_thread::yield();
			}
			else if (thread_ctrl::get_current())
			{
				thread_ctrl::wait_for(repeat_duration_ms * 1000);
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(repeat_duration_ms));
			}
		}
	}
}
