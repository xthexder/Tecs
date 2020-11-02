#include <Tecs.hh>

#include "timer.hh"
#include "test_components.hh"

#include <chrono>
#include <cstring>
#include <thread>

using namespace benchmark;

std::atomic_bool running;
static Tecs::ECS<Transform, Renderable, Script> ecs;

#define ENTITY_COUNT 1000000
#define THREAD_COUNT 0

#define TRANSFORM_DIVISOR 3
#define RENDERABLE_DIVISOR 3
#define SCRIPT_DIVISOR 10

void renderThread() {
	MultiTimer timer("RenderThread");
	MultiTimer timer2("RenderThread Aquired");
	std::string maxName;
	double maxValue = 0;
	size_t goodCount = 0;
	while (running) {
		auto start = std::chrono::high_resolution_clock::now();
		std::vector<std::string> bad;
		{
			Timer t(timer);
			auto readLock = ecs.ReadEntitiesWith<Renderable, Transform>();
			Timer t2(timer2);
			auto &validRenderables = readLock.ValidIndexes<Renderable>();
			auto &validTransforms = readLock.ValidIndexes<Transform>();
			auto &validIndexes = validRenderables.size() > validTransforms.size() ? validTransforms : validRenderables;
			for (auto i : validIndexes) {
				if (readLock.Has<Renderable, Transform>(i)) {
					auto &renderable = readLock.Get<Renderable>(i);
					auto &transform = readLock.Get<Transform>(i);
					if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
						bad.emplace_back(renderable.name);
					} else {
						goodCount++;
						if (transform.pos[0] > maxValue) {
							maxValue = transform.pos[0];
							maxName = renderable.name;
						}
					}
				}
			}
		}
		std::this_thread::sleep_until(start + std::chrono::milliseconds(11));
	}
	std::cout << "[RenderThread] Max value (" << maxName << ") at " << maxValue << ", Good: " << goodCount << std::endl;
}

// static std::atomic_size_t scriptWorkerQueue;
// static std::atomic<ComponentSetWriteTransaction<decltype(ecs), Script> *> scriptLock;

void scriptWorkerThread(bool master) {
	MultiTimer timer("ScriptWorkerThread", master);
	MultiTimer timer2("ScriptWorkerThread Aquired", master);
	while (running) {
		auto start = std::chrono::high_resolution_clock::now();
		/*{
			Timer t(timer);
			if (master) {
				ComponentSetWriteTransaction<decltype(ecs), Script> writeLock(ecs);
				scriptWorkerQueue = 0;
				scriptLock = &writeLock;

				Timer t2(timer2);
				while (running) {
					size_t entIndex = scriptWorkerQueue++;
					if (entIndex < validScripts.size()) {
						auto &script = scriptComponents[validScripts[entIndex]];
						// "Run" script
						for (int i = 0; i < script.data.size(); i++) {
							script.data[i]++;
						}
					} else {
						break;
					}
				}

				while (running && scriptWorkerQueue < THREAD_COUNT + validScripts.size()) {
					std::this_thread::yield();
				}

				scriptLock = nullptr;
			} else {
				auto &validScripts = scriptLock.WriteValidIndexes();
				auto &scriptComponents = scripts.WriteComponents();

				while (running) {
					size_t entIndex = scriptWorkerQueue++;
					if (entIndex < validScripts.size()) {
						auto &script = scriptComponents[validScripts[entIndex]];
						// "Run" script
						for (int i = 0; i < script.data.size(); i++) {
							script.data[i]++;
						}
					} else {
						break;
					}
				}
			}
		}*/
		std::this_thread::sleep_until(start + std::chrono::milliseconds(11));
	}
}

void transformWorkerThread() {
	MultiTimer timer("TransformWorkerThread");
	MultiTimer timer2("TransformWorkerThread Aquired");
	while (running) {
		auto start = std::chrono::high_resolution_clock::now();
		{
			Timer t(timer);
			auto writeLock = ecs.ModifyEntitiesWith<false, Transform>();
			Timer t2(timer2);
			auto &validTransforms = writeLock.ValidIndexes<Transform>();
			for (auto i : validTransforms) {
				auto &transform = writeLock.Get<Transform>(i);
				transform.pos[0]++;
				transform.pos[1]++;
				transform.pos[2]++;
			}
		}
		std::this_thread::sleep_until(start + std::chrono::milliseconds(11));
	}
}

int main(int argc, char **argv) {
	{
		Timer t("Create entities");
		auto writeLock = ecs.ModifyEntitiesWith<true, Transform, Renderable, Script>();
		for (size_t i = 0; i < ENTITY_COUNT; i++) {
			size_t id = writeLock.AddEntity();
			if (i % TRANSFORM_DIVISOR == 0) { writeLock.Set<Transform>(id, 0.0, 0.0, 0.0, 1); }
			if (i % RENDERABLE_DIVISOR == 0) { writeLock.Set<Renderable>(id, "entity" + std::to_string(i)); }
			if (i % SCRIPT_DIVISOR == 0) {
				writeLock.Set<Script>(id, std::initializer_list<uint8_t>({0, 0, 0, 0, 0, 0, 0, 0}));
			}
		}
	}
    
    std::cout << "Running with " << ecs.GetComponentCount() << " component types" << std::endl;

	{
		Timer t("Run threads");
		running = true;

		std::thread threads[2 + THREAD_COUNT];
		threads[0] = std::thread(renderThread);
		threads[1] = std::thread(transformWorkerThread);
		for (size_t i = 0; i < THREAD_COUNT; i++) {
			threads[2 + i] = std::thread(scriptWorkerThread, i == 0);
		}

		std::this_thread::sleep_for(std::chrono::seconds(10));

		running = false;

		threads[0].join();
		threads[1].join();
		for (size_t i = 0; i < THREAD_COUNT; i++) {
			threads[2 + i].join();
		}
	}

	{
		Timer t("Validate entities");
		int invalid = 0;
		int valid = 0;
		double commonValue;
		auto readLock = ecs.ReadEntitiesWith<Transform>();
		auto &validIndexes = readLock.ValidIndexes<Transform>();
		for (auto i : validIndexes) {
			auto transform = readLock.Get<Transform>(i);
			if (transform.pos[0] != transform.pos[1] || transform.pos[1] != transform.pos[2]) {
				if (invalid == 0) {
					std::cerr << "Component is not in correct place! " << transform.pos[0] << ", " << transform.pos[1]
							  << ", " << transform.pos[2] << std::endl;
				}
				invalid++;
			} else {
				if (valid == 0) {
					commonValue = transform.pos[0];
				} else if (transform.pos[0] != commonValue) {
					if (invalid == 0) {
						std::cerr << "Component is not in correct place! " << transform.pos[0] << ", "
								  << transform.pos[1] << ", " << transform.pos[2] << std::endl;
					}
					invalid++;
				}
				valid++;
			}
		}
		if (invalid != 0) { std::cerr << "Error: " << std::to_string(invalid) << " invalid components" << std::endl; }
		std::cout << validIndexes.size() << " total components (" << valid << " with value " << commonValue << ")"
				  << std::endl;
	}

	return 0;
}
