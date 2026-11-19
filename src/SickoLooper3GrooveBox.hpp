// Rack-side endpoint for the GrooveBox looper protocol (MIDI channel 16).
#pragma once
#include <array>

namespace looper3 {
constexpr int NO_BUTTON = -1;
constexpr int ALL_STOP = -2;

#ifndef METAMODULE
struct PadInput : midi::InputQueue {
	void onMessage(const midi::Message& msg) override {
		if (msg.getSize() != 3 || msg.getChannel() != 15)
			return;
		const int key = msg.getNote();
		const bool action = msg.getStatus() == 0x9 && msg.getValue() > 0 && key >= 36 && key < 84;
		const bool knob = msg.getStatus() == 0xb && key >= 20 && key < 36;
		if (action || knob)
			InputQueue::onMessage(msg);
	}
};

// Module-dependent parameter IDs keep all button actions on the original panel/CV paths.
template <class M>
struct GrooveBox {
	PadInput input;
	midi::Output output;
	midi::Message rx, tx;
	std::array<int, 28> current{}, sent{};
	int feedbackCursor = 0;
	int64_t nextSample = 0, nextSend = 0, nextHeartbeat = 0;
	int lastDevice = -1, lastDriver = -1;

	GrooveBox() {
		input.setDeviceId(-1);
		output.setDeviceId(-1);
		input.setChannel(15);
		output.setChannel(15);
		tx.setStatus(0xb);
		tx.setChannel(15);
	}

	void save(json_t* j) {
		json_object_set_new(j, "grooveboxInput", input.toJson());
		json_object_set_new(j, "grooveboxOutput", output.toJson());
	}

	void load(json_t* j) {
		if (auto* port = json_object_get(j, "grooveboxInput"))
			input.fromJson(port);
		if (auto* port = json_object_get(j, "grooveboxOutput"))
			output.fromJson(port);
		input.setChannel(15);
		output.setChannel(15);
	}

	static int knobParam(int k) {
		static const int ids[] = {
			M::MASTERVOL_KNOB_PARAM, M::BPM_KNOB_PARAM,
			M::VOLTRACK_KNOB_PARAM+2, M::PAN_KNOB_PARAM+2,
			M::VOLTRACK_KNOB_PARAM+1, M::PAN_KNOB_PARAM+1,
			M::VOLTRACK_KNOB_PARAM,   M::PAN_KNOB_PARAM,
			M::EARVOL_KNOB_PARAM, M::CLICKVOL_KNOB_PARAM,
			M::MEAS_KNOB_PARAM+2, M::XFADE_KNOB_PARAM+2,
			M::MEAS_KNOB_PARAM+1, M::XFADE_KNOB_PARAM+1,
			M::MEAS_KNOB_PARAM,   M::XFADE_KNOB_PARAM
		};
		return ids[k];
	}

	void turn(M& m, int k, int value) {
		if (value == 0 || value == 64)
			return;
		int delta = value < 64 ? value : value - 128;
		// One tick: 1% level, 2% pan, 1 BPM (10 native units), 1 measure or 1 ms.
		static const float steps[] = {.01f,10.f,.01f,.02f,.01f,.02f,.01f,.02f,
			.01f,.02f,1.f,1.f,1.f,1.f,1.f,1.f};
		auto* q = m.paramQuantities[knobParam(k)];
		q->setImmediateValue(q->getImmediateValue() + steps[k] * delta);
	}

	static void toggle(M& m, int id) {
		auto* q = m.paramQuantities[id];
		q->setImmediateValue(q->getImmediateValue() == q->minValue ? q->maxValue : q->minValue);
	}

	int press(M& m, int note) {
		const int page = (note - 36) / 16;
		const int row = ((note - 36) / 4) % 4;
		const int col = (note - 36) % 4;
		if (row < 3) {
			if (page == 0 && col < 3) {
				static const int buttons[] = {M::REC_BUT_PARAM, M::PLAY_BUT_PARAM, M::STOP_BUT_PARAM};
				return buttons[col] + row;
			}
			if (page == 1 && col == 3)
				return M::ERASE_BUT_PARAM + row;
			static const int switches[][4] = {
				{0, 0, 0, M::SOLO_SWITCH},
				{M::REV_SWITCH, M::ONESHOT_SWITCH, M::LOOPSYNC_SWITCH, 0},
				{M::STARTIMM_SWITCH, M::STOPIMM_SWITCH, M::SRC_TO_TRACK_SWITCH, M::MUTE_SWITCH}
			};
			toggle(m, switches[page][col] + row);
		}
		else if (page == 1) {
			switch (col) {
				case 0: return M::CLOCK_RST_SWITCH;
				case 1: toggle(m, M::CLICKTOMASTER_SWITCH); break;
				case 2: toggle(m, M::PREROLL_SWITCH); break;
				case 3: sent.fill(-1); nextSample = nextHeartbeat = 0; break;
			}
		}
		else {
			switch (col) {
				case 0: return M::ALLSTARTSTOP_BUT_PARAM;
				case 1: return ALL_STOP;
				case 2: toggle(m, M::CLICK_BUT_PARAM); break;
				case 3: toggle(m, M::PREROLL_BUT_PARAM); break;
			}
		}
		return NO_BUTTON;
	}

	void sampleFeedback(M& m) {
		int sourceMutes = 0;
		for (int t = 0; t < 3; ++t) {
			current[t] = m.trackStatus[t];
			current[3+t] = m.nextStatus[t];
			const int switches[] = {M::SOLO_SWITCH, M::REV_SWITCH, M::ONESHOT_SWITCH, M::LOOPSYNC_SWITCH,
				M::STARTIMM_SWITCH, M::STOPIMM_SWITCH, M::SRC_TO_TRACK_SWITCH};
			int bits = 0;
			for (int bit = 0; bit < 7; ++bit)
				if (m.params[switches[bit]+t].getValue() > .5f)
					bits |= 1 << bit;
			current[6+t] = bits;
			if (m.params[M::MUTE_SWITCH+t].getValue() > .5f)
				sourceMutes |= 1 << t;
		}
		current[9] = (m.globalStatus != IDLE)
			| ((m.params[M::CLICK_BUT_PARAM].getValue() > .5f) << 1)
			| ((m.params[M::PREROLL_BUT_PARAM].getValue() > .5f) << 2)
			| ((m.params[M::CLICKTOMASTER_SWITCH].getValue() > .5f) << 3)
			| ((m.params[M::PREROLL_SWITCH].getValue() > 1.5f) << 4)
			| ((m.globalStatus == PREROLLING) << 5);
		current[10] = sourceMutes;
		current[11] = 1; // Protocol version and heartbeat.
		for (int k = 0; k < 16; ++k) {
			auto* q = m.paramQuantities[knobParam(k)];
			current[12+k] = std::lround(127.f * math::clamp(
				(m.params[q->paramId].getValue() - q->minValue) / (q->maxValue - q->minValue), 0.f, 1.f));
		}
	}

	void feedback(M& m, const typename M::ProcessArgs& args) {
		// Sample at 50 Hz, emitting at most one changed CC every 2 ms.
		if (args.frame < nextSend)
			return;
		nextSend = args.frame + std::max(1, int(args.sampleRate * .002f));
		int device = output.getDeviceId(), driver = output.getDriverId();
		if (device != lastDevice || driver != lastDriver) {
			lastDevice = device;
			lastDriver = driver;
			sent.fill(-1);
			nextSample = nextHeartbeat = 0;
		}
		if (device < 0)
			return;
		if (args.frame >= nextSample) {
			nextSample = args.frame + int(args.sampleRate * .02f);
			sampleFeedback(m);
		}
		if (args.frame >= nextHeartbeat) {
			nextHeartbeat = args.frame + int(args.sampleRate);
			sent[11] = -1;
		}
		for (size_t n = 0; n < current.size(); ++n) {
			int i = feedbackCursor;
			feedbackCursor = (feedbackCursor + 1) % current.size();
			if (sent[i] == current[i])
				continue;
			tx.setFrame(args.frame);
			tx.setNote(40+i);
			tx.setValue(current[i]);
			output.sendMessage(tx);
			sent[i] = current[i];
			break;
		}
	}

	int process(M& m, const typename M::ProcessArgs& args) {
		// Poll once per 16 samples. A button pulse lasts one sample, so a second
		// tap always gets its own edge, even if both were in the same MIDI packet.
		if (args.frame % 16 != 0)
			return NO_BUTTON;
		feedback(m, args);
		if (input.tryPop(&rx, args.frame)) {
			if (rx.getStatus() == 0x9)
				return press(m, rx.getNote());
			turn(m, rx.getNote() - 20, rx.getValue());
		}
		return NO_BUTTON;
	}
};

// Port choices use Rack's device names and persistence. Channel is fixed by protocol.
inline void portMenu(ui::Menu* menu, midi::Port* port) {
	menu->addChild(createSubmenuItem("Driver", "", [port](ui::Menu* drivers) {
		for (int id : midi::getDriverIds()) {
			drivers->addChild(createCheckMenuItem(midi::getDriver(id)->getName(), "",
				[=] { return port->getDriverId() == id; }, [=] { port->setDriverId(id); }));
		}
	}));
	menu->addChild(createCheckMenuItem("Disconnected", "", [=] { return port->getDeviceId() == -1; },
		[=] { port->setDeviceId(-1); }));
	for (int id : port->getDeviceIds())
		menu->addChild(createCheckMenuItem(port->getDeviceName(id), "", [=] { return port->getDeviceId() == id; },
			[=] { port->setDeviceId(id); }));
}
#endif
} // namespace looper3
