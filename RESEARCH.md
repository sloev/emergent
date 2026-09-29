# Research: Prior Work and Ideas

[← README](README.md)

Background, prior work and ideas not yet built. Nothing here is a claim about what
the project does; the [README](README.md) is.

## Where each mechanism comes from

| Mechanism in `organism.h` | Source |
|---|---|
| Reward = reduction of squared homeostatic need | Keramati & Gutkin 2014, homeostatic reinforcement learning |
| Satiety: more of a good thing stops being pleasant | Cabanac 1971, alliesthesia |
| Curiosity as learning progress, not prediction error | Schmidhuber 1991/2010; Oudeyer, Kaplan & Hafner 2007 (intelligent adaptive curiosity) |
| Count-based novelty of places | Bellemare et al. 2016; place cells: O'Keefe & Nadel 1978 |
| Fixed random recurrent brain, learned readout | reservoir computing (Jaeger 2001; Maass et al. 2002); reward-modulated readout learning: Hoerzer, Legenstein & Maass 2014 |
| Dopamine as reward-prediction error; three-factor rule with eligibility traces | Schultz, Dayan & Montague 1997; Gerstner et al. 2018 |
| Hormones as the knobs of learning and exploration | Doya 2002, metalearning and neuromodulation |
| Pleasure of being a cause (engagement) | White 1959, effectance motivation |
| Evolved instincts, refined by learning | Baldwin effect; Hinton & Nowlan 1987 |
| Two-speed habits and sleep consolidation (planned) | complementary learning systems: McClelland, McNaughton & O'Reilly 1995; replay during sleep |
| Signaling only when both sides gain | Lewis 1969 signaling games; Lowe et al. 2019 |

## Ideas not yet built

- **Fatigue** as a need: recent effort per output, relieved by rest (M3).
- **Sleep**: consolidation while docked and calm (M2).
- **Proprioceptive heading**: stepper step counts or a gyro integrated into the place
  situation, so places carry direction.
- **Social curiosity**: two organisms in one room; measure information between one's
  light/sound/radio and the other's state (M4).
- **Stigmergy**: leaving traces (light, RF tags) that later change behavior.
- **Morphology co-evolution**: evolving the body plan along with the genome in the
  simulator.

## Historical and Conceptual Background

### Cybernetics and Regulation

Norbert Wiener's *[Cybernetics: Or Control and Communication in the Animal and the Machine](https://en.wikipedia.org/wiki/Cybernetics)* framed intelligent behavior as **regulation through feedback** rather than symbolic inference. A thermostat does not "understand" weather; it maintains temperature by sensing deviations and acting to reduce them.

Key ideas this project draws on:

- **Homeostasis.** Maintain internal variables within viable bands.
- **Allostasis.** Change parameters in anticipation of future demands, not just reactions to deviations.

Biology offers countless examples: temperature regulation, blood glucose control, osmotic balance. Homeostatic variables drift; organisms act to restore them. Here, drives emerge from **deviations** of internal variables from preferred ranges and bias actions that historically reduce those deviations.

### Grey Walter's Tortoises

In the late 1940s, W. Grey Walter built electro-mechanical "tortoises" (Elmer and Elsie) coupling photocells and touch sensors directly to motors. Without digital computation, they appeared to explore, avoid obstacles, and return to a hutch to "feed" on light and recharge. These robots remain canonical examples that simple analog control loops can yield rich behavior.

This architecture inherits Walter's emphasis on **tight sensorimotor coupling** but extends it with explicit internal physiology, memory, and a programmable microcontroller substrate.

### Braitenberg Vehicles and Synthetic Psychology

Valentino Braitenberg's *[Vehicles: Experiments in Synthetic Psychology](https://mitpress.mit.edu/9780262521123/vehicles/)* showed how minimal wiring patterns from sensors to motors produce strikingly interpretable behaviors: approach, avoidance, "aggression", "fear", "curiosity".

The key methodological lesson is **synthetic psychology**: build simple systems bottom-up and ask which psychological descriptions humans spontaneously apply. The firmware itself never uses notions like "goal" or "emotion"; those remain observer vocabulary.

### Behavior-Based Robotics and Subsumption

Rodney Brooks' [subsumption architecture](https://people.csail.mit.edu/brooks/papers/robust-layers.pdf) used layers of simple behaviors, where higher layers suppressed lower-level outputs. There was no central world model; behavior emerged from layer interactions and the environment.

Retained here: locality and concurrency, incremental layering on real hardware. Avoided: explicit "behavior modules" (*wander*, *avoid*, *dock*) — instead there are sensor streams, actuator channels, and statistics of their correlations in context.

### Intrinsic Motivation and Homeostatic Reinforcement Learning

In computational neuroscience and RL, **homeostatic reinforcement learning** (HRL) treats the maintenance of internal variables as the underlying objective of behavior. Keramati and Gutkin's work formalizes how classical RL can be grounded in physiological drives, explaining risk aversion, satiation, and other behavioral regularities.(Keramati & Gutkin, 2014)

Recent work such as Yoshida & Kuniyoshi's "Embodied Neural Homeostat" demonstrates that homeostasis-driven RL can yield **integrated behaviors** like walking, foraging, and temperature control in real robots, using internal energy and temperature dynamics as primary signals.(Synthesising Integrated Robot Behaviour through Reinforcement Learning for Homeostasis | bioRxiv, 2024)

This platform is intentionally lighter: a linear readout of a small fixed recurrent network, trained online by one three-factor rule, on a microcontroller. Conceptually, it sits between hand-crafted behavior trees and fully learned policies.

### Embodied Cognition, Ethology, and Stigmergy

Embodied cognition argues that cognition is not only "in the brain" but distributed across **body and environment**. Ethology emphasizes that behavior must be understood in the ecological niche. Concepts like **stigmergy** show how indirect traces (pheromone trails, footprints) mediate social coordination.

The idea, if realized: robots living in physical rooms with light, RF fields, obstacles, and humans; emitting chemical, acoustic, and visual traces; responding to these traces later as if they "meant" something. All semantics would be imposed post-hoc by observers — the firmware only ever sees correlations and contingencies.

### Game Theory, Economics, and Foraging

Several concepts from economics and game theory parallel this design:

- **Optimal foraging theory** treats animal foraging as a utility-maximization problem under uncertainty and depletion; modern RL work reproduces patch-selection behavior with deep agents in continuous environments.(Yoshida et al., 2024)
- **Utility concavity** and **risk aversion** in economic theory mirror homeostatic drive concavity: as an internal variable approaches its comfortable range, the marginal value of further gains diminishes, encouraging risk-averse choices.(Keramati & Gutkin, 2014)
- **Mixed strategies** in evolutionary game theory correspond to **stochastic policies** in RL and to this project's use of controlled randomness: never fully deterministic, always exploring.

This architecture adopts these indirectly: needs are convex (squared) around set-points, exploration noise acts like a mixed strategy over embodied moves, and a TD value estimate over senses and places does the bookkeeping.

### Emergent Behavior in Animal-Inspired Robotics

A growing literature puts animal-inspired robots into real environments, using them as **physical models** of behavior. An overview by Gómez-Marín and colleagues summarizes work on social interaction, vocal production, and goal-directed reaching in neurorobotics, arguing that robots and animals share "skin in the game": friction, noise, wear, and embodiment.(Gomez-Marin & Zhang, 2022)

Closer to this project, recent systems like the Embodied Neural Homeostat (ENH) show emergence of integrated behavior (locomotion, foraging, thermal regulation) from homeostatic drives in physical robots.(Synthesising Integrated Robot Behaviour through Reinforcement Learning for Homeostasis | bioRxiv, 2024)

Swarm-level studies use evolutionary algorithms (e.g., NEAT) to evolve local controllers in simulation that yield group patterns such as flocking, collective transport, or coordinated patrolling.(“Learning Emergent Behavior in Robot Swarms with NEAT,” 2023)

Compared with these, this project emphasizes **resource-constrained microcontrollers**, avoids large neural networks and external training, treats **every signal, including battery**, as uninterpreted at the sensor level, and focuses on **single-agent but socially and ecologically situated** behavior, with optional extensions to groups.

---

## Emergent Phenomena and Cross-Disciplinary Links (Speculative)

None of the following is implemented. These are ideas for what *might* be worth building or measuring later, not capabilities.

### Mechanical Voice and Prosody

Repeated action sequences that improve curiosity/arousal-related drives could in principle produce distinct acoustic signatures — rising/falling motor tones, rhythmic tapping, broadband "grind" pulses. From linguistics and music theory one could borrow prosodic contours (rise–fall, emphasis), rhythmic motifs, call-and-response structures. A possible experiment: expose robots to human rhythmic patterns (clapping, tapping) and observe whether particular action-and-sound sequences become more likely after "successful" interactions. Not implemented; no voice/audio-generation subsystem exists.

### Individual Differences and "Personality"

Because exploration is stochastic, memory is lossy, and spatial histories differ, nominally identical robots *should* develop distinct behavioral profiles — what behavioral ecology calls "behavioral syndromes". Candidate measurements: risk tolerance (distance to obstacles, collision rate), exploration rate (spatial coverage), sociality (time near other agents), persistence (how long patterns are repeated). In the simulator the genome is the obvious source of such differences; none of these metrics are computed yet.

Psychology and psychiatry provide a rich vocabulary to interpret perturbations: altering decay rates or drive gains might yield lethargic, manic, compulsive, or avoidant "personalities" — untested.

### Social and Crowd-Level Effects

With many robots in a shared environment, stigmergic cues (scent, RF, light patterns) could become shared media; simple local rules might yield clustering, segregation, or flocking-like motion; human movement could influence robot distributions and vice versa. This would connect to sociology (norm formation, crowd dynamics), collective behavior in animals, and economics of congestion/resource competition. Swarm-robotics work using evolutionary methods and NEAT to generate emergent group behavior offers a useful comparison point.(“Learning Emergent Behavior in Robot Swarms with NEAT,” 2023) Unimplemented: no multi-robot support yet (M4), no stigmergic channel.

### Game Design, Art, and Education

From game design: crafting legible behavior signatures humans can read and respond to, tuning "reward" landscapes (via environment design) to elicit interesting emergent strategies. From art: robots as kinetic sculptures, emergent narratives formed by their trajectories and interactions, audio-visual aesthetics using mechanically produced sound and light. From education: concrete demonstrations of homeostasis, adaptation, dynamical systems, emergence; visualization tools mapping internal variables to colors, shapes, or sounds. Application ideas, not features.

---

## Modality Ideas (Speculative)

None of these sensors or actuators exist on the
reference boards, and none of these behaviors has been observed.

### Light and Phototaxis-Like Behavior

**Inputs:** `light_*`, `temperature`.  
**Outputs:** motors, servos, LEDs, heater.

Emergent possibilities:

- drift toward brightness (windows, lamps) while boredom dominates,  
- retreat from heat if heat is declared to hurt,  
- internal variables entrained to day/night cycles, yielding activity rhythms.

Self-emitted LEDs can support **self-inspection** and **inter-robot signaling** when robots see each other’s flashes.

### RF Fields as Digital Gradients

**Inputs:** `rf_rssi_*`.  
**Outputs:** locomotion, RF beacons.

RF strength often correlates with human presence, power availability, or network access. The system may discover that:

- some locations yield higher `rf_rssi` and later improvements to energy-related variables,  
- beacons define **digital territories**.

From a sociology and urban-studies perspective, these are analogues of public squares, Wi-Fi hotspots, and infrastructure hubs.

### Chemosensation and Scent Trails

**Inputs:** `gas`, `humidity`, etc.  
**Outputs:** `pump`, `atomizer`, brush or fan.

Robots can:

- lay down “chemical signatures” where certain drives improved,  
- later follow or avoid regions with those signatures,  
- converge on trail networks analogous to ant foraging paths, shaped by evaporation and deposition.

This is direct hardware for stigmergy experiments.

### Touch and Collision

**Inputs:** `touch_*`, sudden IMU changes.  
**Outputs:** motors, vibration, LEDs, sound.

Collisions are never special-cased. They are just patterns where:

- certain touch or IMU channels spike when certain actions are taken,  
- those spikes are declared `hurts`, or correlate with later changes in hunger.

From repeated exposure, the system can:

- favor motion patterns that reduce the chance of these patterns,  
- or, under some experimental manipulations, seek them.

This aligns with classical conditioning and simple risk-avoidance or risk-seeking learning.

### Acoustic and Vibrational Coupling

**Inputs:** microphone bands, peak frequency.  
**Outputs:** `grind_motor`, `vibe_motor`, general motion.

Uses:

- self-monitoring of actuator health (motor sound spectra),  
- emergent “mechanical voice” where actuation patterns become expressive,  
- inter-robot signaling through coded bursts of vibration or tone sequences.

This connects to bioacoustics and human–robot interaction via tapping, clapping, or speech-like patterns.

---

## What This Project Would Illustrate, If It Works

This architecture is a bet that:

1. **Believable creature-like behavior does not require big models.** Carefully structured local feedback, homeostatic drives, and bounded memory can produce behavior that observers interpret using rich psychological language.
2. **Semantics can emerge from statistics.** Battery, collision, and RF signals start as untyped numbers. If they systematically affect internal variables, the hope is that the system comes to behave *as if* it understood "danger", "safety", or "charging", without symbolic representation.
3. **Energy and ecology can be coupled without scripting goals.** The charging station is designed as an attractor in sensor and energy space; "seeking the charger" would be an emergent pattern, not a coded routine.
4. **Microcontrollers are sufficient for serious synthetic ethology.** By trading off explicit value functions and large networks for simpler homeostatic and statistical machinery, interesting experiments should run entirely on-device.
5. **Robots can be used as physical thought experiments about behavior**, in the tradition of animal-inspired robots and homeostatic RL blurring lines between robotics, neuroscience, and ethology.(Synthesising Integrated Robot Behaviour through Reinforcement Learning for Homeostasis | bioRxiv, 2024)(Gomez-Marin & Zhang, 2022)

None of these five are demonstrated yet.

### Possible Further Steps

- **Richer physiology.** More internal variables (e.g., "temperature comfort") and their interactions.
- **Multi-agent experiments.** Several robots with shared or conflicting drives and simple stigmergic channels (scent, light, RF tags).
- **Task-free vs. task-biased regimes.** Same architecture, different environment statistics, compared to explicit task-focused RL agents in the same morphology.
- **Bridges to formal RL.** Compare against HRL agents in the same morphology.(Keramati & Gutkin, 2014)
- **Open-world benchmarks.** Shared environments, metrics, and logging formats so others can run comparable experiments on their own microcontroller platforms.

---

## Related Projects and Prior Art in Practice

While this project stresses **extreme on-device simplicity**, it sits in a broader ecosystem of work on emergent behavior and embodied agents:

- **Homeostatic RL in robots.** Yoshida & Kuniyoshi's "Embodied Neural Homeostat" implements deep HRL on a physical quadruped, achieving emergent walking, foraging, and temperature regulation under homeostatic objectives.(Synthesising Integrated Robot Behaviour through Reinforcement Learning for Homeostasis | bioRxiv, 2024) Several studies model animal-like long-term nutritional behavior using homeostatic RL in simulated agents.(Yoshida et al., 2024)
- **Embodied co-design surveys.** The *Embodied Co-Design for Rapidly Evolving Agents* survey compiles work on jointly optimizing morphology and control, often with deep RL and evolution, and links to multiple open-source implementations (e.g., DERL, emergent hand morphology).(Wang, 2024/2026) These tend to operate in simulation with heavy compute, but share the emphasis on body-environment loops.
- **Swarm emergent behavior with NEAT.** Work on *Learning Emergent Behavior in Robot Swarms with NEAT* evolves controllers for agents whose local rules yield collective patterns.(“Learning Emergent Behavior in Robot Swarms with NEAT,” 2023) Many of these controllers are available in public GitHub repositories (e.g., swarm benchmarks in CoppeliaSim), though typically targeting desktops or simulators.
- **Animal-inspired neurorobotics.** A collection of neurorobotics papers shows robots as models for social interaction, vocalization, and goal-directed reaching, with code often accompanying publications via lab GitHub accounts.(Gomez-Marin & Zhang, 2022)

Compared with these, this project's contribution (once built out) would be: an explicit, end-to-end specification of a pure-emergence architecture tuned for ESP32-class boards; an emphasis on semantic symmetry of all sensory channels (even battery); a concrete, low-profile charging station design intended to support emergent self-charging; systematic ablation of every mechanism in simulation.

---

## Philosophical Boundaries

This project stays at the levels of **behavior** (patterns of motion and interaction) and **organismic organization** (regulation, memory, embodiment).

It does *not* claim subjective experience, moral status, or a solution to consciousness.

The intent is a **concrete, tunable dynamical system** where things like "caring" about battery or avoiding collisions would arise from low-level statistical structure and feedback, and where observers can apply concepts from biology, psychology, economics, and sociology without those concepts being present in code.

---

## References

Classics and conceptual background:

- Norbert Wiener, *Cybernetics: Or Control and Communication in the Animal and the Machine*, 1948.
- W. Grey Walter, *The Living Brain*, 1953.
- Valentino Braitenberg, *Vehicles: Experiments in Synthetic Psychology*, [MIT Press](https://mitpress.mit.edu/9780262521123/vehicles/), 1984.
- Rodney A. Brooks, “A robust layered control system for a mobile robot,” *IEEE Journal of Robotics and Automation*, 1986.
- Warren S. McCulloch, W. Ross Ashby, and others on early homeostatic machine designs.
- J. J. Gibson, *The Ecological Approach to Visual Perception*, 1979.
- J. O’Keefe & L. Nadel, *The Hippocampus as a Cognitive Map*, 1978.
- P.-P. Grassé, “La théorie de la stigmergie,” *Insectes Sociaux*, 1959.

Homeostatic RL and animal-like behavior:

- Mehdi Keramati & Boris Gutkin, “A Reinforcement Learning Theory for Homeostatic Regulation,” *NeurIPS 2011*.
- Mehdi Keramati & Boris Gutkin, “[Homeostatic reinforcement learning for integrating reward collection and physiological stability](https://elifesciences.org/articles/04811),” *eLife* 3:e04811, 2014.(Keramati & Gutkin, 2014)
- S. Yoshida & Y. Kuniyoshi, “Synthesising integrated robot behaviour through reinforcement learning for homeostasis (Embodied Neural Homeostat),” bioRxiv, 2024.(Synthesising Integrated Robot Behaviour through Reinforcement Learning for Homeostasis | bioRxiv, 2024)
- H. Yoshida et al., “Modeling long-term nutritional behaviors using deep homeostatic reinforcement learning,” *PNAS Nexus* (open-access preprint).(Yoshida et al., 2024)

Emergent behavior and neurorobotics:

- A. Gómez-Marín, “[Editorial: Emergent Behavior in Animal-Inspired Robotics](https://www.frontiersin.org/articles/10.3389/fnbot.2022.861831/full),” *Frontiers in Neurorobotics*, 2022.(Gomez-Marin & Zhang, 2022)
- M. Reséndiz-Benhumea et al., “Testing the social brain hypothesis with minimal models in robotics,” *Frontiers in Neurorobotics*, 2021.
- A. Amador & G. B. Mindlin, “Low-dimensional biomechanical model of birdsong production,” *Frontiers in Neuroscience*, 2021.

Swarm and emergent control:

- “Learning Emergent Behavior in Robot Swarms with NEAT,” arXiv:2309.14663, 2023.(“Learning Emergent Behavior in Robot Swarms with NEAT,” 2023)
- E. Pagello et al., “Emergent behaviors of a robot team performing cooperative tasks,” *Advanced Robotics*, 2003.
- Pranav Rajbhandari, *Swarm CoppeliaSim* (GitHub).

Embodied co-design and morphology:

- Y. Wang et al., “[Embodied Co-Design for Rapidly Evolving Agents: Taxonomy, Frontiers, and Challenges](https://github.com/Yuxing-Wang-THU/SurveyBrainBody),” survey with code links, 2023.(Wang, 2024/2026)

And many more in the overlapping literatures of artificial life, developmental robotics, and believable agents.
