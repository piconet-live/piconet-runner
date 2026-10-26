"""A build's own run may make a half-made part again (scripts/release-build.sh): AGAIN=1 for
port/build/build-module.sh; PROFILE=none builds a module without a profile. Its dry runs in a made-up work folder:
nothing is built and no game is started. (The profile's training, runs/profile.sh, is the gates': their tests.)"""
import os, shutil, subprocess, tempfile, unittest

root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
EXISTS = "exists; a build is never overwritten: choose another name"


@unittest.skipUnless(shutil.which("wslpath"), "needs WSL's wslpath")
class Again(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = self.tmp.name
        clean = {k: v for k, v in os.environ.items() if k not in ("AGAIN", "RUNNER", "PROFILE", "BUNDLE", "PROGRESS_RUN")}
        self.env = {**clean, "PORT_WORK": self.work, "PORT_LOCKS": self.work}

    def dry(self, script, **env):
        done = subprocess.run([os.path.join(root, script), "t1", "--dry-run"], env={**self.env, **env}, capture_output=True, text=True)
        return done.stdout + done.stderr

    def test_a_module_that_is_there_is_refused_unless_it_is_the_callers_own_try(self):
        os.makedirs(os.path.join(self.work, "work", "mod-cg-t1"))
        os.makedirs(os.path.join(self.work, "work", "cg-t1"))
        self.assertEqual(self.dry("port/build/build-module.sh").count(EXISTS), 2)
        self.assertEqual(self.dry("port/build/build-module.sh", AGAIN="1").count(EXISTS), 0)

    def test_a_module_without_a_profile_is_built_only_when_asked(self):
        self.assertIn("PROFILE=<file> is required", self.dry("port/build/build-module.sh"))
        said = self.dry("port/build/build-module.sh", PROFILE="none")
        self.assertNotIn("PROFILE=<file> is required", said)
        self.assertNotIn("no profile none", said)
        self.assertIn(" PROFILE= BUNDLE=", said)                                   # no --profile-use


if __name__ == "__main__":
    unittest.main()
