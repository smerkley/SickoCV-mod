// Standalone Linux regression for the actual plugin, using an in-memory Rack MIDI driver.
// See docs/GrooveBox.md for the build command. Run with assertions enabled.
#include <engine/Engine.hpp>
#include <engine/Module.hpp>
#include <midi.hpp>
#include <plugin/Model.hpp>
#include <plugin/Plugin.hpp>
#include <array>
#include <memory>
#include <cassert>
#include <cstdio>
#include <dlfcn.h>

using namespace rack;

struct PadInput : midi::InputDevice {
	std::string getName() override { return "Test pad"; }
};

struct PadOutput : midi::OutputDevice {
	std::array<int, 128> cc;
	int messages = 0;

	PadOutput() { cc.fill(-1); }
	std::string getName() override { return "Test pad"; }

	void sendMessage(const midi::Message& message) override {
		assert(message.getSize() == 3 && message.bytes[0] == 0xbf);
		cc[message.getNote()] = message.getValue();
		++messages;
	}
};

struct TestDriver : midi::Driver {
	PadInput source;
	PadOutput sink;

	std::string getName() override { return "Test driver"; }
	std::vector<int> getInputDeviceIds() override { return {0}; }
	std::vector<int> getOutputDeviceIds() override { return {0}; }
	std::string getInputDeviceName(int) override { return source.getName(); }
	std::string getOutputDeviceName(int) override { return sink.getName(); }

	midi::InputDevice* subscribeInput(int, midi::Input* port) override {
		source.subscribe(port);
		return &source;
	}
	midi::OutputDevice* subscribeOutput(int, midi::Output* port) override {
		sink.subscribe(port);
		return &sink;
	}
	void unsubscribeInput(int, midi::Input* port) override { source.unsubscribe(port); }
	void unsubscribeOutput(int, midi::Output* port) override { sink.unsubscribe(port); }
};

int main(int argc, char** argv) {
	if (argc != 2) {
		std::fprintf(stderr, "Usage: groovebox-test <directory containing plugin.so and res>\n");
		return 2;
	}
	constexpr float rate = 44100.f;
	Context context;
	contextSet(&context);
	context.engine = new engine::Engine;
	context.engine->setSampleRate(rate);
	auto* driver = new TestDriver;
	constexpr int driverId = 67;
	midi::addDriver(driverId, driver);

	plugin::Plugin plugin;
	plugin.path = argv[1];
	auto library = dlopen((plugin.path + "/plugin.so").c_str(), RTLD_NOW);
	if (!library) {
		std::fprintf(stderr, "%s\n", dlerror());
		return 1;
	}
	auto init = reinterpret_cast<void (*)(plugin::Plugin*)>(dlsym(library, "init"));
	assert(init);
	init(&plugin);
	auto* model = plugin.getModel("SickoLooper3");
	assert(model);
	std::unique_ptr<engine::Module> module(model->createModule());
	module->onSampleRateChange({rate, 1.f / rate});

	// Restore the same port data a user's Rack patch would contain.
	auto ports = json_pack("{s:{s:i,s:s},s:{s:i,s:s}}",
		"grooveboxInput", "driver", driverId, "deviceName", "Test pad",
		"grooveboxOutput", "driver", driverId, "deviceName", "Test pad");
	module->dataFromJson(ports);
	json_decref(ports);
	assert(driver->source.subscribed.size() == 1 && driver->sink.subscribed.size() == 1);

	int64_t frame = 0;
	auto run = [&](int frames) {
		for (int i = 0; i < frames; ++i, ++frame)
			module->process({rate, 1.f / rate, frame});
	};
	auto send = [&](uint8_t status, uint8_t number, uint8_t value) {
		midi::Message message;
		message.bytes = {status, number, value};
		message.frame = frame;
		driver->source.onMessage(message);
		run(12000);
	};
	auto press = [&](int note) { send(0x9f, note, 127); };
	const auto& cc = driver->sink.cc;
	auto awaitValue = [&](int number, int value) {
		for (int i = 0; i < 3000 && cc[number] != value; ++i)
			run(96);
		if (cc[number] != value)
			std::fprintf(stderr, "CC %d expected %d, received %d\n", number, value, cc[number]);
		assert(cc[number] == value);
	};

	press(67); // Request full state.
	for (int number = 40; number <= 67; ++number)
		assert(cc[number] >= 0);
	int flags = cc[49];
	send(0x90, 50, 127); // Same command on channel 1.
	send(0x9f, 50, 0);   // Velocity-zero release.
	assert(cc[49] == flags);
	press(50);
	assert(cc[49] == (flags ^ 2));
	send(0xbf, 26, 127); // Turn loop 1 volume down by one detent.
	assert(cc[58] < 127);
	for (auto* param : module->paramQuantities) {
		if (param && param->name == "Track Volume") {
			param->setImmediateValue(.5f);
			break;
		}
	}
	awaitValue(58, 64);

	press(54); // Free-length recording.
	press(71); // Unmute source 1.
	module->inputs[0].channels = 1;
	module->inputs[0].setVoltage(2.f);
	press(36);
	awaitValue(40, 4);
	press(38);
	awaitValue(40, 1);
	module->inputs[0].setVoltage(0.f);
	press(37);
	awaitValue(40, 3);
	float peak = 0.f;
	for (int i = 0; i < 16000; ++i) {
		run(1);
		peak = std::max(peak, module->outputs[0].getVoltage());
	}
	assert(peak > .4f); // Playback comes from the recorded buffer.
	press(36);
	awaitValue(40, 5);
	press(49); // Stop all with one loop overdubbing and two empty.
	awaitValue(40, 1);
	assert(cc[44] == 0 && cc[45] == 0);
	press(55);
	assert(cc[40] == 1); // First erase press arms the native double tap.
	press(55);
	awaitValue(40, 0);

	send(0xbf, 32, 8); // Extend loop lengths to observe overlapping recordings.
	send(0xbf, 30, 8);
	press(51);
	press(40); // Pre-roll before loop 2 records.
	awaitValue(44, 4);
	awaitValue(41, 4);
	press(44);
	awaitValue(42, 4);
	assert(cc[40] == 0);
	std::printf("All three loop control rows passed. Recorded playback peak %.3f V.\n", peak);
}
