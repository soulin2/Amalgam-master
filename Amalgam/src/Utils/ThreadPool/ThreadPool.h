#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class CThreadPool
{
private:
	std::vector<std::thread> m_vWorkers;
	std::queue<std::function<void()>> m_qTasks;
	std::mutex m_mtxQueue;
	std::condition_variable m_cvCondition;
	std::atomic<bool> m_bStop{ false };
	int m_iWorkerCount = 0;

public:
	void Initialize()
	{
		m_iWorkerCount = std::max(2, static_cast<int>(std::thread::hardware_concurrency()) - 1);
		m_bStop = false;

		for (int i = 0; i < m_iWorkerCount; i++)
		{
			m_vWorkers.emplace_back([this]
			{
				while (true)
				{
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(m_mtxQueue);
						m_cvCondition.wait(lock, [this] { return m_bStop || !m_qTasks.empty(); });
						if (m_bStop && m_qTasks.empty())
							return;
						task = std::move(m_qTasks.front());
						m_qTasks.pop();
					}
					task();
				}
			});
		}
	}

	void Shutdown()
	{
		{
			std::unique_lock<std::mutex> lock(m_mtxQueue);
			m_bStop = true;
		}
		m_cvCondition.notify_all();
		for (auto& worker : m_vWorkers)
		{
			if (worker.joinable())
				worker.join();
		}
		m_vWorkers.clear();
	}

	template<typename F>
	auto Submit(F&& f) -> std::future<decltype(f())>
	{
		using ReturnType = decltype(f());
		auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<F>(f));
		std::future<ReturnType> result = task->get_future();
		{
			std::unique_lock<std::mutex> lock(m_mtxQueue);
			if (m_bStop)
				return result;
			m_qTasks.emplace([task]() { (*task)(); });
		}
		m_cvCondition.notify_one();
		return result;
	}

	// Parallel for over range [iBegin, iEnd) with chunk-based work distribution
	template<typename F>
	void ParallelFor(int iBegin, int iEnd, F&& func)
	{
		const int iTotal = iEnd - iBegin;
		if (iTotal <= 0)
			return;

		// For small workloads, run inline
		if (iTotal <= 4 || m_iWorkerCount == 0)
		{
			for (int i = iBegin; i < iEnd; i++)
				func(i);
			return;
		}

		const int iChunks = std::min(m_iWorkerCount, iTotal);
		const int iChunkSize = (iTotal + iChunks - 1) / iChunks;

		std::vector<std::future<void>> vFutures;
		vFutures.reserve(iChunks);

		for (int c = 0; c < iChunks; c++)
		{
			int iStart = iBegin + c * iChunkSize;
			int iStop = std::min(iStart + iChunkSize, iEnd);
			if (iStart >= iEnd)
				break;

			vFutures.push_back(Submit([iStart, iStop, &func]
			{
				for (int i = iStart; i < iStop; i++)
					func(i);
			}));
		}

		for (auto& f : vFutures)
			f.get();
	}

	int GetWorkerCount() const { return m_iWorkerCount; }
	bool IsRunning() const { return !m_bStop && !m_vWorkers.empty(); }

	~CThreadPool() { if (!m_bStop) Shutdown(); }
};

ADD_FEATURE_CUSTOM(CThreadPool, ThreadPool, U);
